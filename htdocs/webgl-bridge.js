/*
 * webgl-bridge.js — Browser WebAssembly bridge for WebGL 1/2 graphics.
 * Provides the "webgl" WASM import module.
 *
 * Offloads asynchronous GLSL shader compilation and program linking to a
 * dedicated background WebGL Shader Compiler Worker (when Worker + OffscreenCanvas
 * are available) so that heavy shader compilation never stalls the WASM simulation
 * worker or UI interaction.
 */

function readStr(memory, ptr, len) {
	if (!memory || !ptr || len <= 0) return '';
	return new TextDecoder().decode(new Uint8Array(memory.buffer, ptr >>> 0, len >>> 0));
}

function getExport(exports, names) {
	if (!exports) return null;
	for (const name of names) {
		if (typeof exports[name] === 'function') return exports[name];
	}
	return null;
}

/* ── Self-Contained Shader Pre-Compiler Worker Script ── */
const SHADER_WORKER_SOURCE = `
let gl = null;
let ext = null;
const pending = [];
let polling = false;

function getCompilerGl() {
	if (gl) return gl;
	if (typeof OffscreenCanvas === 'undefined') return null;
	try {
		const c = new OffscreenCanvas(1, 1);
		gl = c.getContext('webgl2') || c.getContext('webgl');
		if (gl && typeof gl.getExtension === 'function') {
			ext = gl.getExtension('KHR_parallel_shader_compile');
		}
	} catch (_) {
		gl = null;
	}
	return gl;
}

function pollPending() {
	polling = false;
	if (!gl || pending.length === 0) return;
	for (let i = pending.length - 1; i >= 0; i--) {
		const item = pending[i];
		if (ext) {
			const done = gl.getProgramParameter(item.prog, 0x91B1);
			if (!done) continue;
		}
		const ok = !!gl.getProgramParameter(item.prog, gl.LINK_STATUS);
		gl.deleteProgram(item.prog);
		gl.deleteShader(item.vs);
		gl.deleteShader(item.fs);
		pending.splice(i, 1);
		self.postMessage({ type: 'webgl:compiled', programId: item.programId, rev: item.rev, ok });
	}
	if (pending.length > 0) {
		polling = true;
		setTimeout(pollPending, 8);
	}
}

self.onmessage = (evt) => {
	const msg = evt.data;
	if (!msg || msg.type !== 'webgl:compile') return;
	const c = getCompilerGl();
	if (!c) {
		self.postMessage({ type: 'webgl:compiled', programId: msg.programId, rev: msg.rev, ok: true });
		return;
	}
	try {
		const vs = c.createShader(c.VERTEX_SHADER);
		const fs = c.createShader(c.FRAGMENT_SHADER);
		const prog = c.createProgram();
		if (!vs || !fs || !prog) {
			if (vs) c.deleteShader(vs);
			if (fs) c.deleteShader(fs);
			if (prog) c.deleteProgram(prog);
			self.postMessage({ type: 'webgl:compiled', programId: msg.programId, rev: msg.rev, ok: true });
			return;
		}
		c.shaderSource(vs, msg.vsSrc || '');
		c.compileShader(vs);
		c.shaderSource(fs, msg.fsSrc || '');
		c.compileShader(fs);
		c.attachShader(prog, vs);
		c.attachShader(prog, fs);
		if (Array.isArray(msg.attribBinds)) {
			for (const [idx, name] of msg.attribBinds) {
				c.bindAttribLocation(prog, idx, name);
			}
		}
		c.linkProgram(prog);
		if (typeof c.flush === 'function') c.flush();
		pending.push({ programId: msg.programId, rev: msg.rev, prog, vs, fs });
		if (!polling) {
			polling = true;
			setTimeout(pollPending, 4);
		}
	} catch (_) {
		self.postMessage({ type: 'webgl:compiled', programId: msg.programId, rev: msg.rev, ok: true });
	}
};
`;

export function createWebGlBridge(root, getWasm) {
	let gl = null;
	let glCanvas = null;
	let kicked = false;
	const shaders = [null];
	const shaderMeta = [null];
	const programs = [null];
	const programMeta = [null];
	const buffers = [null];
	const bufferMeta = [null];
	const textures = [null];
	const textureMeta = [null];
	const framebuffers = [null];
	const framebufferMeta = [null];
	const renderbuffers = [null];
	const renderbufferMeta = [null];
	let currentBoundFramebufferId = 0;
	let currentBoundRenderbufferId = 0;
	const uniformLocs = [null];
	const uniformMeta = [null];
	const globalScope = typeof window !== 'undefined' ? window : globalThis;

	let fallbackOffscreen = null;
	let shaderWorker = null;
	let shaderWorkerUnavailable = false;
	let compileRevCounter = 1;

	function getShaderWorker() {
		if (shaderWorker) return shaderWorker;
		if (shaderWorkerUnavailable) return null;
		if (typeof Worker === 'undefined' || typeof OffscreenCanvas === 'undefined' || typeof Blob === 'undefined' || typeof URL === 'undefined' || typeof URL.createObjectURL !== 'function') {
			shaderWorkerUnavailable = true;
			return null;
		}
		try {
			const blob = new Blob([SHADER_WORKER_SOURCE], { type: 'application/javascript' });
			const url = URL.createObjectURL(blob);
			shaderWorker = new Worker(url);
			URL.revokeObjectURL(url);
			shaderWorker.onmessage = (evt) => {
				const msg = evt.data;
				if (!msg || msg.type !== 'webgl:compiled') return;
				const pm = programMeta[msg.programId];
				if (!pm || pm.compileRev !== msg.rev) return;
				pm.workerCompiling = false;
				pm.workerDone = true;
				pm.workerOk = !!msg.ok;
				if (!pm.workerOk) {
					pm.localReady = true;
					pm.localLinkedOk = false;
					return;
				}
				const c = getGl();
				if (c) {
					finalizeLocalProgramLink(c, msg.programId);
				}
			};
			shaderWorker.onerror = () => {
				shaderWorkerUnavailable = true;
				shaderWorker = null;
				for (let i = 1; i < programMeta.length; i++) {
					const pm = programMeta[i];
					if (pm && pm.workerCompiling) {
						pm.workerCompiling = false;
						pm.workerDone = true;
						pm.workerOk = true;
						const c = getGl();
						if (c) finalizeLocalProgramLink(c, i);
					}
				}
			};
			return shaderWorker;
		} catch (_) {
			shaderWorkerUnavailable = true;
			shaderWorker = null;
			return null;
		}
	}

	function findCanvas() {
		const scope = (root && root.querySelector) ? root : (typeof document !== 'undefined' ? document : null);
		if (scope) {
			let c = scope.querySelector('[data-bud-webgl]') ||
				scope.querySelector('[data-bud-canvas-fit]') ||
				scope.querySelector('[data-bud-canvas]') ||
				scope.querySelector('canvas');
			if (!c && typeof document !== 'undefined' && scope !== document) {
				c = document.querySelector('[data-bud-webgl]') ||
					document.querySelector('[data-bud-canvas-fit]') ||
					document.querySelector('[data-bud-canvas]') ||
					document.querySelector('canvas');
			}
			return c;
		}
		if (globalScope.__stele_worker_canvases instanceof Map) {
			let webglMatch = null, fitMatch = null;
			for (const entry of globalScope.__stele_worker_canvases.values()) {
				if (!entry || !entry.canvas) continue;
				if (!webglMatch && entry.webgl) webglMatch = entry.canvas;
				if (!fitMatch && entry.fit) fitMatch = entry.canvas;
			}
			if (webglMatch || fitMatch) return webglMatch || fitMatch;
			if (typeof OffscreenCanvas !== 'undefined') {
				if (!fallbackOffscreen) {
					fallbackOffscreen = new OffscreenCanvas(640, 480);
					fallbackOffscreen.__stele_fit = true;
				}
				return fallbackOffscreen;
			}
		}
		return null;
	}

	let parallelCompileExt = undefined;

	function finalizeLocalProgramLink(c, programId) {
		const pm = programMeta[programId];
		if (!pm || !pm.linked) return;
		let p = programs[programId];
		if (!p) {
			p = c.createProgram();
			programs[programId] = p;
		}
		if (!p) return;
		for (const sid of pm.shaders) {
			const sm = shaderMeta[sid];
			if (!sm) continue;
			let s = shaders[sid];
			if (!s) {
				s = c.createShader(sm.type);
				shaders[sid] = s;
			}
			if (s && sm.src) {
				c.shaderSource(s, sm.src);
				c.compileShader(s);
				c.attachShader(p, s);
			}
		}
		for (const [idx, name] of pm.attribBinds) {
			c.bindAttribLocation(p, idx, name);
		}
		c.linkProgram(p);
		pm.localLinkedInitiated = true;
		if (!parallelCompileExt) {
			pm.localReady = true;
			pm.localLinkedOk = !!c.getProgramParameter(p, c.LINK_STATUS);
		}
	}

	function rebuildContextResources(c) {
		for (let i = 1; i < shaderMeta.length; i++) {
			const sm = shaderMeta[i];
			if (!sm) { shaders[i] = null; continue; }
			const s = c.createShader(sm.type);
			if (s && sm.src) {
				c.shaderSource(s, sm.src);
			}
			shaders[i] = s;
		}
		for (let i = 1; i < programMeta.length; i++) {
			const pm = programMeta[i];
			if (!pm) { programs[i] = null; continue; }
			const p = c.createProgram();
			programs[i] = p;
			if (p && pm.linked && !pm.workerCompiling) {
				pm.localLinkedInitiated = false;
				pm.localReady = false;
				finalizeLocalProgramLink(c, i);
			}
		}
		for (let i = 1; i < uniformMeta.length; i++) {
			const um = uniformMeta[i];
			if (!um) { uniformLocs[i] = null; continue; }
			const p = programs[um.programId];
			const pm = programMeta[um.programId];
			uniformLocs[i] = (p && pm && pm.localLinkedInitiated) ? c.getUniformLocation(p, um.name) : null;
		}
		for (let i = 1; i < bufferMeta.length; i++) {
			const bm = bufferMeta[i];
			if (!bm) { buffers[i] = null; continue; }
			const b = c.createBuffer();
			if (b && bm.target) {
				c.bindBuffer(bm.target, b);
				if (bm.data) {
					c.bufferData(bm.target, bm.data, bm.usage);
				} else if (bm.size > 0) {
					c.bufferData(bm.target, bm.size, bm.usage);
				}
			}
			buffers[i] = b;
		}
		for (let i = 1; i < textureMeta.length; i++) {
			const tm = textureMeta[i];
			if (!tm) { textures[i] = null; continue; }
			const t = c.createTexture();
			if (t && tm.target) {
				c.bindTexture(tm.target, t);
				for (const [pname, param] of tm.params) {
					c.texParameteri(tm.target, pname, param);
				}
				if (tm.width > 0 && tm.height > 0) {
					c.texImage2D(tm.target, tm.level, tm.internalformat, tm.width, tm.height, tm.border, tm.format, tm.type, tm.pixels);
				}
			}
			textures[i] = t;
		}
		for (let i = 1; i < renderbufferMeta.length; i++) {
			const rm = renderbufferMeta[i];
			if (!rm) { renderbuffers[i] = null; continue; }
			const r = c.createRenderbuffer();
			if (r && rm.width > 0 && rm.height > 0) {
				c.bindRenderbuffer(c.RENDERBUFFER, r);
				c.renderbufferStorageMultisample(c.RENDERBUFFER, rm.samples, rm.internalformat, rm.width, rm.height);
			}
			renderbuffers[i] = r;
		}
		for (let i = 1; i < framebufferMeta.length; i++) {
			const fm = framebufferMeta[i];
			if (!fm) { framebuffers[i] = null; continue; }
			const f = c.createFramebuffer();
			framebuffers[i] = f;
			c.bindFramebuffer(c.FRAMEBUFFER, f);
			for (const a of fm.attachments) {
				if (a.rb) c.framebufferRenderbuffer(a.target, a.attachment, c.RENDERBUFFER, renderbuffers[a.id] || null);
				else c.framebufferTexture2D(a.target, a.attachment, a.textarget, textures[a.id] || null, a.level);
			}
		}
		c.bindFramebuffer(c.FRAMEBUFFER, null);
		currentBoundFramebufferId = 0;
	}

	function getGl() {
		const canvas = findCanvas();
		if (gl && glCanvas && glCanvas.isConnected !== false && (!canvas || canvas === glCanvas)) return gl;
		if (canvas) {
			const prevCanvas = glCanvas;
			/* Default framebuffer is single-sampled: scenes antialias in their own multisampled FBO and resolve-blit into it
			 * (glBlitFramebuffer into a multisampled draw framebuffer is INVALID_OPERATION). */
			const glOpts = { preserveDrawingBuffer: true, alpha: true, antialias: false, depth: true };
			gl = canvas.getContext('webgl2', glOpts) || canvas.getContext('webgl', glOpts);
			glCanvas = canvas;
			parallelCompileExt = (gl && typeof gl.getExtension === 'function')
				? gl.getExtension('KHR_parallel_shader_compile')
				: null;
			if (prevCanvas && prevCanvas !== canvas && gl) {
				rebuildContextResources(gl);
			}
			if (!kicked && typeof requestAnimationFrame === 'function') {
				kicked = true;
				const fn = getExport(getWasm ? getWasm() : null, ['wasm_canvas_render']);
				if (fn) requestAnimationFrame(() => fn());
			}
		}
		return gl;
	}

	let currentBoundArrayBufferId = 0;
	let currentBoundElementBufferId = 0;
	let currentBoundTextureId = 0;

	return {
		mount() {
			getGl();
		},
		destroy() {
			gl = null;
			glCanvas = null;
			parallelCompileExt = undefined;
			if (shaderWorker) {
				try { shaderWorker.terminate(); } catch (_) {}
				shaderWorker = null;
			}
		},
		makeImports() {
			return {
				glViewport(x, y, w, h) {
					const c = getGl();
					if (!c) return;
					const isFit = glCanvas && (glCanvas.__stele_fit || (glCanvas.hasAttribute && glCanvas.hasAttribute('data-bud-canvas-fit')));
					if (isFit && w > 0 && h > 0) {
						if (glCanvas.width !== w || glCanvas.height !== h) {
							glCanvas.width = w;
							glCanvas.height = h;
						}
					}
					c.viewport(x, y, w, h);
				},
				glClearColor(r, g, b, a) {
					const c = getGl();
					if (c) c.clearColor(r, g, b, a);
				},
				glClear(mask) {
					const c = getGl();
					if (c) {
						c.clear(mask);
						if (glCanvas) glCanvas.__stele_dirty = true;
					}
				},
				glEnable(cap) {
					const c = getGl();
					if (c) c.enable(cap);
				},
				glDisable(cap) {
					const c = getGl();
					if (c) c.disable(cap);
				},
				glDepthFunc(func) {
					const c = getGl();
					if (c) c.depthFunc(func);
				},
				glDepthMask(flag) {
					const c = getGl();
					if (c) c.depthMask(!!flag);
				},
				glBlendFunc(sfactor, dfactor) {
					const c = getGl();
					if (c) c.blendFunc(sfactor, dfactor);
				},
				glColorMask(r, g, b, a) {
					const c = getGl();
					if (c) c.colorMask(!!r, !!g, !!b, !!a);
				},
				glPolygonOffset(factor, units) {
					const c = getGl();
					if (c) c.polygonOffset(factor, units);
				},
				glStencilFunc(func, ref, mask) {
					const c = getGl();
					if (c) c.stencilFunc(func, ref, mask >>> 0);
				},
				glStencilOp(sfail, dpfail, dppass) {
					const c = getGl();
					if (c) c.stencilOp(sfail, dpfail, dppass);
				},
				glStencilMask(mask) {
					const c = getGl();
					if (c) c.stencilMask(mask >>> 0);
				},
				glClearStencil(s) {
					const c = getGl();
					if (c) c.clearStencil(s);
				},
				glCreateShader(type) {
					const c = getGl();
					if (!c) return 0;
					const s = c.createShader(type);
					shaders.push(s);
					shaderMeta.push({ type, src: '', compiled: false, deleted: false });
					return shaders.length - 1;
				},
				glShaderSource(shaderId, srcPtr, srcLen) {
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory) return;
					const src = readStr(exports.memory, srcPtr, srcLen);
					const s = shaders[shaderId];
					if (shaderMeta[shaderId]) shaderMeta[shaderId].src = src;
					if (s) c.shaderSource(s, src);
				},
				glCompileShader(shaderId) {
					const c = getGl();
					const s = shaders[shaderId];
					if (shaderMeta[shaderId]) shaderMeta[shaderId].compiled = true;
					const w = getShaderWorker();
					if (!w && c && s) {
						c.compileShader(s);
					}
				},
				glGetShaderParameter(shaderId, pname) {
					const c = getGl();
					const sm = shaderMeta[shaderId];
					if (!c || !sm) return 0;
					const w = getShaderWorker();
					if (w) return 1;
					const s = shaders[shaderId];
					if (!s) return 0;
					if (pname === 0x91B1 && !parallelCompileExt) return 1;
					const res = c.getShaderParameter(s, pname);
					return res ? 1 : 0;
				},
				glCreateProgram() {
					const c = getGl();
					if (!c) return 0;
					const p = c.createProgram();
					programs.push(p);
					programMeta.push({
						shaders: [],
						attribBinds: [],
						linked: false,
						workerCompiling: false,
						workerDone: false,
						workerOk: true,
						localLinkedInitiated: false,
						localReady: false,
						localLinkedOk: true,
						compileRev: 0
					});
					return programs.length - 1;
				},
				glAttachShader(programId, shaderId) {
					const c = getGl();
					const pm = programMeta[programId];
					if (pm) pm.shaders.push(shaderId);
					const w = getShaderWorker();
					if (!w) {
						const p = programs[programId];
						const s = shaders[shaderId];
						if (c && p && s) c.attachShader(p, s);
					}
				},
				glLinkProgram(programId) {
					const c = getGl();
					const pm = programMeta[programId];
					if (!pm) return;
					pm.linked = true;
					const w = getShaderWorker();
					if (w) {
						let vsSrc = '';
						let fsSrc = '';
						for (const sid of pm.shaders) {
							const sm = shaderMeta[sid];
							if (!sm) continue;
							if (sm.type === 0x8B31) vsSrc = sm.src;
							else if (sm.type === 0x8B30) fsSrc = sm.src;
						}
						const rev = compileRevCounter++;
						pm.compileRev = rev;
						pm.workerCompiling = true;
						pm.workerDone = false;
						pm.localLinkedInitiated = false;
						pm.localReady = false;
						w.postMessage({
							type: 'webgl:compile',
							programId,
							rev,
							vsSrc,
							fsSrc,
							attribBinds: pm.attribBinds
						});
						return;
					}
					if (c) {
						const p = programs[programId];
						if (p) {
							c.linkProgram(p);
							pm.localLinkedInitiated = true;
							if (!parallelCompileExt) {
								pm.localReady = true;
								pm.localLinkedOk = !!c.getProgramParameter(p, c.LINK_STATUS);
							}
						}
					}
				},
				glGetProgramParameter(programId, pname) {
					const c = getGl();
					const p = programs[programId];
					const pm = programMeta[programId];
					if (!c || !pm) return 0;
					if (pname === 0x91B1) {
						if (pm.workerCompiling) return 0;
						if (!pm.localLinkedInitiated) {
							if (pm.workerDone && !pm.workerOk) return 1;
							finalizeLocalProgramLink(c, programId);
						}
						if (pm.localReady) return 1;
						if (!parallelCompileExt) {
							pm.localReady = true;
							pm.localLinkedOk = p ? !!c.getProgramParameter(p, c.LINK_STATUS) : false;
							return 1;
						}
						const done = p ? !!c.getProgramParameter(p, 0x91B1) : true;
						if (done) {
							pm.localReady = true;
							pm.localLinkedOk = p ? !!c.getProgramParameter(p, c.LINK_STATUS) : false;
						}
						return done ? 1 : 0;
					}
					if (pname === 0x8B82) {
						if (pm.workerDone && !pm.workerOk) return 0;
						if (!pm.localLinkedInitiated && !pm.workerCompiling) {
							finalizeLocalProgramLink(c, programId);
						}
						if (pm.localReady) return pm.localLinkedOk ? 1 : 0;
						return (p && c.getProgramParameter(p, pname)) ? 1 : 0;
					}
					if (!p) return 0;
					const res = c.getProgramParameter(p, pname);
					return res ? 1 : 0;
				},
				glUseProgram(programId) {
					const c = getGl();
					const p = programs[programId];
					if (c) c.useProgram(p || null);
				},
				glCreateBuffer() {
					const c = getGl();
					if (!c) return 0;
					const b = c.createBuffer();
					buffers.push(b);
					bufferMeta.push({ target: 0, data: null, size: 0, usage: 0 });
					return buffers.length - 1;
				},
				glBindBuffer(target, bufferId) {
					const c = getGl();
					if (target === 0x8892) currentBoundArrayBufferId = bufferId;
					else if (target === 0x8893) currentBoundElementBufferId = bufferId;
					const b = buffers[bufferId];
					if (c) c.bindBuffer(target, b || null);
				},
				glBufferData(target, dataPtr, size, usage) {
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory) return;
					const bid = target === 0x8892 ? currentBoundArrayBufferId : (target === 0x8893 ? currentBoundElementBufferId : 0);
					const uptr = dataPtr >>> 0;
					const usize = size >>> 0;
					if (!uptr) {
						if (bid && bufferMeta[bid]) {
							bufferMeta[bid].target = target;
							bufferMeta[bid].data = null;
							bufferMeta[bid].size = usize;
							bufferMeta[bid].usage = usage;
						}
						c.bufferData(target, usize, usage);
						return;
					}
					const view = new Uint8Array(exports.memory.buffer, uptr, usize);
					if (bid && bufferMeta[bid]) {
						bufferMeta[bid].target = target;
						bufferMeta[bid].size = usize;
						bufferMeta[bid].usage = usage;
						bufferMeta[bid].data = (usage === 0x88E4) ? new Uint8Array(view) : null;
					}
					c.bufferData(target, view, usage);
				},
				glCreateTexture() {
					const c = getGl();
					if (!c) return 0;
					const t = c.createTexture();
					textures.push(t);
					textureMeta.push({ target: 0, params: [], level: 0, internalformat: 0, width: 0, height: 0, border: 0, format: 0, type: 0, pixels: null });
					return textures.length - 1;
				},
				glDeleteTexture(texId) {
					const c = getGl();
					const t = textures[texId];
					if (c && t) c.deleteTexture(t);
					if (texId > 0 && texId < textures.length) {
						textures[texId] = null;
						textureMeta[texId] = null;
					}
				},
				glBindTexture(target, texId) {
					const c = getGl();
					currentBoundTextureId = texId;
					const t = texId > 0 ? textures[texId] : null;
					if (c) c.bindTexture(target, t || null);
				},
				glActiveTexture(texture) {
					const c = getGl();
					if (c) c.activeTexture(texture);
				},
				glTexParameteri(target, pname, param) {
					const c = getGl();
					if (currentBoundTextureId && textureMeta[currentBoundTextureId]) {
						textureMeta[currentBoundTextureId].target = target;
						textureMeta[currentBoundTextureId].params.push([pname, param]);
					}
					if (c) c.texParameteri(target, pname, param);
				},
				glPixelStorei(pname, param) {
					const c = getGl();
					if (c) c.pixelStorei(pname, param);
				},
				glTexImage2D(target, level, internalformat, width, height, border, format, type, pixelsPtr) {
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory) return;
					const ptr = pixelsPtr >>> 0;
					const tm = currentBoundTextureId ? textureMeta[currentBoundTextureId] : null;
					if (!ptr) {
						if (tm) {
							tm.target = target; tm.level = level; tm.internalformat = internalformat;
							tm.width = width; tm.height = height; tm.border = border; tm.format = format; tm.type = type;
							tm.pixels = null;
						}
						c.texImage2D(target, level, internalformat, width, height, border, format, type, null);
						return;
					}
					if (type === 0x1402) {
						const count = width * height;
						const view = new Int16Array(exports.memory.buffer, ptr, count);
						if (tm) {
							tm.target = target; tm.level = level; tm.internalformat = internalformat;
							tm.width = width; tm.height = height; tm.border = border; tm.format = format; tm.type = type;
							tm.pixels = new Int16Array(view);
						}
						c.texImage2D(target, level, internalformat, width, height, border, format, type, view);
					} else {
						const byteLen = width * height * 4;
						const view = new Uint8Array(exports.memory.buffer, ptr, byteLen);
						if (tm) {
							tm.target = target; tm.level = level; tm.internalformat = internalformat;
							tm.width = width; tm.height = height; tm.border = border; tm.format = format; tm.type = type;
							tm.pixels = new Uint8Array(view);
						}
						c.texImage2D(target, level, internalformat, width, height, border, format, type, view);
					}
				},
				glEnableVertexAttribArray(index) {
					const c = getGl();
					if (c) c.enableVertexAttribArray(index);
				},
				glDisableVertexAttribArray(index) {
					const c = getGl();
					if (c) c.disableVertexAttribArray(index);
				},
				glVertexAttribPointer(index, size, type, normalized, stride, offset) {
					const c = getGl();
					if (c) c.vertexAttribPointer(index, size, type, !!normalized, stride, offset >>> 0);
				},
				glDrawArrays(mode, first, count) {
					const c = getGl();
					if (c) {
						c.drawArrays(mode, first, count);
						if (glCanvas) glCanvas.__stele_dirty = true;
					}
				},
				glDrawElements(mode, count, type, offset) {
					const c = getGl();
					if (c) {
						c.drawElements(mode, count, type, offset >>> 0);
						if (glCanvas) glCanvas.__stele_dirty = true;
					}
				},
				glGetAttribLocation(programId, namePtr, nameLen) {
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory) return -1;
					const name = readStr(exports.memory, namePtr, nameLen);
					const p = programs[programId];
					if (!p) return -1;
					return c.getAttribLocation(p, name);
				},
				glBindAttribLocation(programId, index, namePtr, nameLen) {
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory) return;
					const name = readStr(exports.memory, namePtr, nameLen);
					if (programMeta[programId]) programMeta[programId].attribBinds.push([index, name]);
					const p = programs[programId];
					if (p) c.bindAttribLocation(p, index, name);
				},
				glGetUniformLocation(programId, namePtr, nameLen) {
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory) return -1;
					const name = readStr(exports.memory, namePtr, nameLen);
					const p = programs[programId];
					if (!p) return -1;
					const loc = c.getUniformLocation(p, name);
					if (!loc) return -1;
					uniformLocs.push(loc);
					uniformMeta.push({ programId, name });
					return uniformLocs.length - 1;
				},
				glUniform1i(locId, v0) {
					const c = getGl();
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (c && loc) c.uniform1i(loc, v0);
				},
				glUniform1f(locId, v0) {
					const c = getGl();
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (c && loc) c.uniform1f(loc, v0);
				},
				glUniform2f(locId, v0, v1) {
					const c = getGl();
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (c && loc) c.uniform2f(loc, v0, v1);
				},
				glUniform3f(locId, v0, v1, v2) {
					const c = getGl();
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (c && loc) c.uniform3f(loc, v0, v1, v2);
				},
				glUniform4f(locId, v0, v1, v2, v3) {
					const c = getGl();
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (c && loc) c.uniform4f(loc, v0, v1, v2, v3);
				},
				glUniform2fv(locId, count, valPtr) {
					const c = getGl();
					const exports = getWasm();
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (!c || !loc || !exports || !exports.memory) return;
					const floats = new Float32Array(exports.memory.buffer, valPtr >>> 0, count * 2);
					c.uniform2fv(loc, floats);
				},
				glUniform3fv(locId, count, valPtr) {
					const c = getGl();
					const exports = getWasm();
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (!c || !loc || !exports || !exports.memory) return;
					const floats = new Float32Array(exports.memory.buffer, valPtr >>> 0, count * 3);
					c.uniform3fv(loc, floats);
				},
				glUniformMatrix4fv(locId, count, trans, valPtr) {
					const c = getGl();
					const exports = getWasm();
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (!c || !loc || !exports || !exports.memory) return;
					const floats = new Float32Array(exports.memory.buffer, valPtr >>> 0, count * 16);
					c.uniformMatrix4fv(loc, !!trans, floats);
				},
				glDeleteShader(shaderId) {
					const c = getGl();
					const s = shaders[shaderId];
					if (shaderMeta[shaderId]) shaderMeta[shaderId].deleted = true;
					const w = getShaderWorker();
					if (!w) {
						if (c && s) c.deleteShader(s);
						shaders[shaderId] = null;
					}
				},
				glDeleteProgram(programId) {
					const c = getGl();
					const p = programs[programId];
					if (c && p) c.deleteProgram(p);
					programs[programId] = null;
					if (programMeta[programId]) programMeta[programId] = null;
				},
				glDeleteBuffer(bufferId) {
					const c = getGl();
					const b = buffers[bufferId];
					if (c && b) c.deleteBuffer(b);
					buffers[bufferId] = null;
				},
				glCreateFramebuffer() {
					const c = getGl();
					if (!c) return 0;
					framebuffers.push(c.createFramebuffer());
					framebufferMeta.push({ attachments: [] });
					return framebuffers.length - 1;
				},
				glDeleteFramebuffer(fbId) {
					const c = getGl();
					const f = framebuffers[fbId];
					if (c && f) c.deleteFramebuffer(f);
					if (fbId > 0 && fbId < framebuffers.length) { framebuffers[fbId] = null; framebufferMeta[fbId] = null; }
				},
				glBindFramebuffer(target, fbId) {
					const c = getGl();
					if (target === 0x8D40 || target === 0x8CA9) currentBoundFramebufferId = fbId;
					if (c) c.bindFramebuffer(target, fbId > 0 ? (framebuffers[fbId] || null) : null);
				},
				glFramebufferTexture2D(target, attachment, textarget, texId, level) {
					const c = getGl();
					const fm = currentBoundFramebufferId ? framebufferMeta[currentBoundFramebufferId] : null;
					if (fm) {
						fm.attachments = fm.attachments.filter((a) => a.attachment !== attachment);
						fm.attachments.push({ target, attachment, textarget, id: texId, level, rb: false });
					}
					if (c) c.framebufferTexture2D(target, attachment, textarget, texId > 0 ? (textures[texId] || null) : null, level);
				},
				glFramebufferRenderbuffer(target, attachment, rbtarget, rbId) {
					const c = getGl();
					const fm = currentBoundFramebufferId ? framebufferMeta[currentBoundFramebufferId] : null;
					if (fm) {
						fm.attachments = fm.attachments.filter((a) => a.attachment !== attachment);
						fm.attachments.push({ target, attachment, textarget: 0, id: rbId, level: 0, rb: true });
					}
					if (c) c.framebufferRenderbuffer(target, attachment, rbtarget, rbId > 0 ? (renderbuffers[rbId] || null) : null);
				},
				glCheckFramebufferStatus(target) {
					const c = getGl();
					return c ? c.checkFramebufferStatus(target) : 0;
				},
				glCreateRenderbuffer() {
					const c = getGl();
					if (!c) return 0;
					renderbuffers.push(c.createRenderbuffer());
					renderbufferMeta.push({ samples: 0, internalformat: 0, width: 0, height: 0 });
					return renderbuffers.length - 1;
				},
				glDeleteRenderbuffer(rbId) {
					const c = getGl();
					const r = renderbuffers[rbId];
					if (c && r) c.deleteRenderbuffer(r);
					if (rbId > 0 && rbId < renderbuffers.length) { renderbuffers[rbId] = null; renderbufferMeta[rbId] = null; }
				},
				glBindRenderbuffer(target, rbId) {
					const c = getGl();
					currentBoundRenderbufferId = rbId;
					if (c) c.bindRenderbuffer(target, rbId > 0 ? (renderbuffers[rbId] || null) : null);
				},
				glRenderbufferStorageMultisample(target, samples, internalformat, width, height) {
					const c = getGl();
					const rm = currentBoundRenderbufferId ? renderbufferMeta[currentBoundRenderbufferId] : null;
					if (rm) { rm.samples = samples; rm.internalformat = internalformat; rm.width = width; rm.height = height; }
					if (c && typeof c.renderbufferStorageMultisample === 'function') {
						c.renderbufferStorageMultisample(target, samples, internalformat, width, height);
					}
				},
				glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter) {
					const c = getGl();
					if (c && typeof c.blitFramebuffer === 'function') {
						c.blitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
						if (glCanvas) glCanvas.__stele_dirty = true;
					}
				}
			};
		}
	};
}

const _wglGlobal = typeof window !== 'undefined' ? window : globalThis;
_wglGlobal.__wasm_capability_registry = _wglGlobal.__wasm_capability_registry || {};
_wglGlobal.__wasm_capability_registry.webgl = createWebGlBridge;
if (typeof _wglGlobal.registerWasmCapability === 'function') {
	_wglGlobal.registerWasmCapability('webgl', createWebGlBridge);
}
