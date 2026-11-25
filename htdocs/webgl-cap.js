/*
 * webgl-bridge.js — Browser WebAssembly bridge for WebGL 1/2 graphics.
 * Provides the "webgl" WASM import module.
 *
 * Offloads asynchronous GLSL shader compilation and program linking to a
 * dedicated background WebGL Shader Compiler Worker (when Worker + OffscreenCanvas
 * are available) so that heavy shader compilation never stalls the WASM simulation
 * worker or UI interaction.
 */

const TEXT_DECODER = new TextDecoder();

function readStr(memory, ptr, len) {
	if (!memory || !ptr || len <= 0) return '';
	return TEXT_DECODER.decode(new Uint8Array(memory.buffer, ptr >>> 0, len >>> 0));
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
	let currentProgramId = 0;
	let currentUnpackAlignment = 4;
	const attachedShadersByProgram = new WeakMap();
	const uniformLocs = [null];
	const uniformMeta = [null];
	const globalScope = typeof window !== 'undefined' ? window : globalThis;

	let fallbackOffscreen = null;
	let shaderWorker = null;
	let shaderWorkerUnavailable = false;
	let compileRevCounter = 1;

	/* Optional runtime bridge profiler (disabled by default; zero allocations in hot path) */
	let perfEnabled = !!(globalScope && globalScope.__webglPerfEnabled);
	const perfStats = {
		getGlCalls: 0,
		domQueries: 0,
		getUniformLocationCalls: 0,
		uniformUploads: 0,
		texUploads: 0,
		bufferUploads: 0,
		draws: 0,
		blits: 0
	};
	const perfApi = {
		enable(v) { perfEnabled = !!v; },
		isEnabled() { return perfEnabled; },
		reset() {
			perfStats.getGlCalls = 0;
			perfStats.domQueries = 0;
			perfStats.getUniformLocationCalls = 0;
			perfStats.uniformUploads = 0;
			perfStats.texUploads = 0;
			perfStats.bufferUploads = 0;
			perfStats.draws = 0;
			perfStats.blits = 0;
		},
		snapshot() {
			return {
				enabled: perfEnabled,
				...perfStats,
				uniformLocsLen: uniformLocs.length - 1,
				programsLen: programs.length - 1,
				texturesLen: textures.length - 1
			};
		}
	};
	if (globalScope) globalScope.__webglBridgePerf = perfApi;

	function attachShaderOnce(c, p, s, sid) {
		if (!c || !p || !s) return;
		let set = attachedShadersByProgram.get(p);
		if (!set) {
			set = new Set();
			attachedShadersByProgram.set(p, set);
		}
		if (set.has(sid)) return;
		set.add(sid);
		c.attachShader(p, s);
	}

	function refreshUniformLocsForProgram(c, programId) {
		const p = programs[programId];
		const pm = programMeta[programId];
		if (!c || !p || !pm || !pm.localLinkedInitiated) return;
		for (let i = 1; i < uniformMeta.length; i++) {
			const um = uniformMeta[i];
			if (um && um.programId === programId) {
				uniformLocs[i] = c.getUniformLocation(p, um.name);
			}
		}
	}

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
		if (perfEnabled) perfStats.domQueries++;
		const scope = (root && root.querySelector) ? root : (typeof document !== 'undefined' ? document : null);
		if (scope) {
			let c = scope.querySelector('[data-bud-webgl]') ||
				scope.querySelector('[data-canvas-webgl]') ||
				scope.querySelector('[data-bud-canvas-fit]') ||
				scope.querySelector('[data-canvas-fit]');
			if (!c && typeof document !== 'undefined' && scope !== document) {
				c = document.querySelector('[data-bud-webgl]') ||
					document.querySelector('[data-canvas-webgl]') ||
					document.querySelector('[data-bud-canvas-fit]') ||
					document.querySelector('[data-canvas-fit]');
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
	let anisoExt = null;
	let maxAniso = 1;

	function finalizeLocalProgramLink(c, programId) {
		const pm = programMeta[programId];
		if (!pm || !pm.linked) return;
		let p = programs[programId];
		if (!p) {
			p = c.createProgram();
			programs[programId] = p;
		}
		if (!p) return;
		if (pm.linkedProgram === p && pm.localLinkedInitiated) return;
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
				attachShaderOnce(c, p, s, sid);
			}
		}
		for (const [idx, name] of pm.attribBinds) {
			c.bindAttribLocation(p, idx, name);
		}
		c.linkProgram(p);
		pm.linkedProgram = p;
		pm.localLinkedInitiated = true;
		if (!parallelCompileExt) {
			pm.localReady = true;
			pm.localLinkedOk = !!c.getProgramParameter(p, c.LINK_STATUS);
		}
		refreshUniformLocsForProgram(c, programId);
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
			pm.linkedProgram = null;
			pm.localLinkedInitiated = false;
			pm.localReady = false;
			if (p && pm.linked && (!pm.workerCompiling || currentProgramId === i)) {
				if (!(pm.workerDone && !pm.workerOk)) {
					finalizeLocalProgramLink(c, i);
				}
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
		const nullFboTexIds = new Set();
		if (typeof c.activeTexture === 'function') c.activeTexture(0x84C0);
		for (let i = 1; i < textureMeta.length; i++) {
			const tm = textureMeta[i];
			if (!tm) { textures[i] = null; continue; }
			const t = c.createTexture();
			if (t && tm.target) {
				c.bindTexture(tm.target, t);
				if (Array.isArray(tm.mips) && tm.mips.length > 0) {
					let hasPixels = false;
					for (let lvl = 0; lvl < tm.mips.length; lvl++) {
						const m = tm.mips[lvl];
						if (m && m.width > 0 && m.height > 0) {
							if (m.pixels) {
								hasPixels = true;
								c.pixelStorei(c.UNPACK_ALIGNMENT, m.unpackAlignment || 1);
							}
							c.texImage2D(m.target || tm.target, lvl, m.internalformat, m.width, m.height, m.border, m.format, m.type, m.pixels);
						}
					}
					if (!hasPixels && tm.width > 0 && tm.height > 0) {
						nullFboTexIds.add(i);
					}
				} else if (tm.width > 0 && tm.height > 0) {
					if (tm.pixels) c.pixelStorei(c.UNPACK_ALIGNMENT, tm.unpackAlignment || 1);
					else nullFboTexIds.add(i);
					c.texImage2D(tm.target, tm.level, tm.internalformat, tm.width, tm.height, tm.border, tm.format, tm.type, tm.pixels);
				}
				for (const [pname, param] of tm.params) {
					if (pname === 0x84FE) {
						if (anisoExt) c.texParameterf(tm.target, 0x84FE, Math.min(maxAniso, Math.max(1, param)));
					} else {
						c.texParameteri(tm.target, pname, param);
					}
				}
			}
			textures[i] = t;
		}
		c.pixelStorei(c.UNPACK_ALIGNMENT, currentUnpackAlignment);
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
			let clearMask = 0;
			let hasNullTexAtt = false;
			let maxW = 0, maxH = 0;
			for (const a of fm.attachments) {
				if (a.rb) {
					c.framebufferRenderbuffer(a.target, a.attachment, c.RENDERBUFFER, renderbuffers[a.id] || null);
					const rm = renderbufferMeta[a.id];
					if (rm) {
						if (rm.width > maxW) maxW = rm.width;
						if (rm.height > maxH) maxH = rm.height;
					}
				} else {
					c.framebufferTexture2D(a.target, a.attachment, a.textarget, textures[a.id] || null, a.level);
					if (nullFboTexIds.has(a.id)) hasNullTexAtt = true;
					const tm = textureMeta[a.id];
					if (tm) {
						if (tm.width > maxW) maxW = tm.width;
						if (tm.height > maxH) maxH = tm.height;
					}
				}
				if (a.attachment >= 0x8CE0 && a.attachment <= 0x8CFF) clearMask |= 0x4000;
				else if (a.attachment === 0x8D00) clearMask |= 0x0100;
				else if (a.attachment === 0x8D20) clearMask |= 0x0400;
				else if (a.attachment === 0x821A) clearMask |= 0x0500;
			}
			if (hasNullTexAtt && clearMask && typeof c.clear === 'function' && c.checkFramebufferStatus(c.FRAMEBUFFER) === 0x8CD5) {
				if (typeof c.disable === 'function') c.disable(0x0C11);
				if (typeof c.colorMask === 'function') c.colorMask(true, true, true, true);
				if (typeof c.depthMask === 'function') c.depthMask(true);
				if (typeof c.stencilMask === 'function') c.stencilMask(0xFF);
				if (typeof c.clearColor === 'function') c.clearColor(0, 0, 0, 0);
				if (typeof c.clearDepth === 'function') c.clearDepth(1);
				if (typeof c.clearStencil === 'function') c.clearStencil(0);
				if (typeof c.viewport === 'function' && maxW > 0 && maxH > 0) c.viewport(0, 0, maxW, maxH);
				c.clear(clearMask);
			}
		}
		c.bindFramebuffer(c.FRAMEBUFFER, null);
		currentBoundFramebufferId = 0;
		if (typeof c.viewport === 'function' && glCanvas && glCanvas.width > 0 && glCanvas.height > 0) {
			c.viewport(0, 0, glCanvas.width, glCanvas.height);
		}
		if (typeof c.activeTexture === 'function') {
			for (let u = 0; u < 32; u++) {
				const id2d = boundTextures2D[u] || 0;
				const idCube = boundTexturesCube[u] || 0;
				if (u === 0 || id2d > 0) {
					c.activeTexture(0x84C0 + u);
					c.bindTexture(0x0DE1, id2d > 0 ? (textures[id2d] || null) : null);
				}
				if (idCube > 0) {
					c.activeTexture(0x84C0 + u);
					c.bindTexture(0x8513, textures[idCube] || null);
				}
			}
			c.activeTexture(0x84C0 + currentActiveTextureUnit);
		}
		c.bindBuffer(c.ARRAY_BUFFER, currentBoundArrayBufferId > 0 ? (buffers[currentBoundArrayBufferId] || null) : null);
		c.bindBuffer(c.ELEMENT_ARRAY_BUFFER, currentBoundElementBufferId > 0 ? (buffers[currentBoundElementBufferId] || null) : null);
		if (currentProgramId > 0) {
			const p = programs[currentProgramId];
			const pm = programMeta[currentProgramId];
			c.useProgram((p && (!pm || !pm.localReady || pm.localLinkedOk)) ? p : null);
		}
	}

	let isWebGL2 = false;
	let workerCanvasesGen = -1;
	let cachedBuf = null;
	let memU8 = null;
	let memI16 = null;
	let memF32 = null;

	function ensureMemViews(memory) {
		const buf = memory.buffer;
		if (buf !== cachedBuf) {
			cachedBuf = buf;
			memU8 = new Uint8Array(buf);
			memI16 = new Int16Array(buf);
			memF32 = new Float32Array(buf);
		}
	}

	function getGl(forceCheck) {
		if (perfEnabled) perfStats.getGlCalls++;
		const curWGen = globalScope.__stele_worker_canvases_gen || 0;
		if (!forceCheck && gl && glCanvas && glCanvas !== fallbackOffscreen &&
		    glCanvas.isConnected !== false && curWGen === workerCanvasesGen) {
			return gl;
		}
		workerCanvasesGen = curWGen;
		const canvas = findCanvas();
		if (gl && glCanvas && glCanvas.isConnected !== false && (!canvas || canvas === glCanvas)) return gl;
		if (canvas) {
			const prevCanvas = glCanvas;
			/* Default framebuffer is single-sampled: scenes antialias in their own multisampled FBO and resolve-blit into it
			 * (glBlitFramebuffer into a multisampled draw framebuffer is INVALID_OPERATION).
			 * Keep preserveDrawingBuffer off in normal browsers to avoid a full-screen compositor copy per frame;
			 * enable under webdriver/explicit flag so headless screenshot/readPixels capture keeps working. */
			const preserveBuf = !!(globalScope && (globalScope.__webglPreserveDrawingBuffer || (globalScope.navigator && globalScope.navigator.webdriver)));
			const glOpts = { preserveDrawingBuffer: preserveBuf, alpha: true, antialias: false, depth: true };
			gl = canvas.getContext('webgl2', glOpts) || canvas.getContext('webgl', glOpts);
			glCanvas = canvas;
			isWebGL2 = !!(gl && typeof WebGL2RenderingContext !== 'undefined' && gl instanceof WebGL2RenderingContext);
			parallelCompileExt = (gl && typeof gl.getExtension === 'function')
				? gl.getExtension('KHR_parallel_shader_compile')
				: null;
			if (gl && typeof gl.getExtension === 'function') {
				gl.getExtension('EXT_color_buffer_float');
				gl.getExtension('EXT_color_buffer_half_float');
				anisoExt = gl.getExtension('EXT_texture_filter_anisotropic')
					|| gl.getExtension('WEBKIT_EXT_texture_filter_anisotropic')
					|| gl.getExtension('MOZ_EXT_texture_filter_anisotropic');
				maxAniso = anisoExt ? (gl.getParameter(anisoExt.MAX_TEXTURE_MAX_ANISOTROPY_EXT) || 1) : 1;
			}
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
	let currentActiveTextureUnit = 0;
	const boundTextures2D = new Array(32).fill(0);
	const boundTexturesCube = new Array(32).fill(0);

	function boundTextureMap(target) {
		return target === 0x8513 ? boundTexturesCube : boundTextures2D;
	}
	function getBoundTextureId(target) {
		return boundTextureMap(target)[currentActiveTextureUnit] || 0;
	}
	function setBoundTextureId(target, texId) {
		boundTextureMap(target)[currentActiveTextureUnit] = texId | 0;
	}

	return {
		perf: perfApi,
		mount() {
			getGl(true);
		},
		destroy() {
			gl = null;
			glCanvas = null;
			isWebGL2 = false;
			workerCanvasesGen = -1;
			cachedBuf = null;
			memU8 = null;
			memI16 = null;
			memF32 = null;
			parallelCompileExt = undefined;
			anisoExt = null;
			maxAniso = 1;
			currentProgramId = 0;
			currentUnpackAlignment = 4;
			currentActiveTextureUnit = 0;
			boundTextures2D.fill(0);
			boundTexturesCube.fill(0);
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
					if (!currentBoundFramebufferId && isFit && w > 0 && h > 0) {
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
					const w = (!parallelCompileExt) ? getShaderWorker() : null;
					if (!w && c && s) {
						c.compileShader(s);
					}
				},
				glGetShaderParameter(shaderId, pname) {
					const c = getGl();
					const sm = shaderMeta[shaderId];
					if (!c || !sm) return 0;
					const w = (!parallelCompileExt) ? getShaderWorker() : null;
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
						linkedProgram: null,
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
					if (pm && !pm.shaders.includes(shaderId)) pm.shaders.push(shaderId);
					const w = (!parallelCompileExt) ? getShaderWorker() : null;
					if (!w) {
						const p = programs[programId];
						const s = shaders[shaderId];
						attachShaderOnce(c, p, s, shaderId);
					}
				},
				glLinkProgram(programId) {
					const c = getGl();
					const pm = programMeta[programId];
					if (!pm) return;
					pm.linked = true;
					const w = (!parallelCompileExt) ? getShaderWorker() : null;
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
							pm.linkedProgram = p;
							pm.localLinkedInitiated = true;
							if (!parallelCompileExt) {
								pm.localReady = true;
								pm.localLinkedOk = !!c.getProgramParameter(p, c.LINK_STATUS);
							} else {
								pm.localReady = false;
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
						if (parallelCompileExt && p) {
							const done = !!c.getProgramParameter(p, parallelCompileExt.COMPLETION_STATUS_KHR);
							if (!done) return 0;
							pm.localReady = true;
							pm.localLinkedOk = !!c.getProgramParameter(p, c.LINK_STATUS);
							return 1;
						}
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
						if (pm.workerCompiling) return 0;
						if (pm.workerDone && !pm.workerOk) return 0;
						if (!pm.localLinkedInitiated) {
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
					currentProgramId = programId | 0;
					if (!c) return;
					if (currentProgramId <= 0) {
						c.useProgram(null);
						return;
					}
					const p = programs[currentProgramId];
					const pm = programMeta[currentProgramId];
					if (p && pm && pm.linked && !pm.localLinkedInitiated) {
						if (pm.workerDone && !pm.workerOk) {
							c.useProgram(null);
							return;
						}
						finalizeLocalProgramLink(c, currentProgramId);
					}
					if (p && pm && pm.localReady && !pm.localLinkedOk) {
						c.useProgram(null);
						return;
					}
					c.useProgram(p || null);
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
					if (perfEnabled) perfStats.bufferUploads++;
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
				glBufferSubData(target, offset, size, dataPtr) {
					if (perfEnabled) perfStats.bufferUploads++;
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory) return;
					const uoff = offset >>> 0;
					const usize = size >>> 0;
					const uptr = dataPtr >>> 0;
					if (!uptr || !usize) return;
					ensureMemViews(exports.memory);
					const bid = target === 0x8892 ? currentBoundArrayBufferId : (target === 0x8893 ? currentBoundElementBufferId : 0);
					if (bid && bufferMeta[bid] && bufferMeta[bid].data) {
						const dst = bufferMeta[bid].data;
						if (uoff + usize <= dst.byteLength) {
							dst.set(memU8.subarray(uptr, uptr + usize), uoff);
						}
					}
					if (isWebGL2) {
						c.bufferSubData(target, uoff, memU8, uptr, usize);
					} else {
						c.bufferSubData(target, uoff, memU8.subarray(uptr, uptr + usize));
					}
				},
				glCreateTexture() {
					const c = getGl();
					if (!c) return 0;
					const t = c.createTexture();
					if (!t) return 0;
					textures.push(t);
					textureMeta.push({ target: 0, params: [], mips: [], level: 0, internalformat: 0, width: 0, height: 0, border: 0, format: 0, type: 0, unpackAlignment: 4, pixels: null });
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
					for (let u = 0; u < 32; u++) {
						if (boundTextures2D[u] === texId) boundTextures2D[u] = 0;
						if (boundTexturesCube[u] === texId) boundTexturesCube[u] = 0;
					}
				},
				glBindTexture(target, texId) {
					const c = getGl();
					setBoundTextureId(target, texId);
					let t = texId > 0 ? textures[texId] : null;
					if (c && texId > 0 && !t && textureMeta[texId] && !(typeof c.isContextLost === 'function' && c.isContextLost())) {
						t = c.createTexture();
						textures[texId] = t || null;
					}
					if (c) c.bindTexture(target, t || null);
				},
				glActiveTexture(texture) {
					const c = getGl();
					const unit = (texture - 0x84C0) | 0;
					if (unit >= 0 && unit < 32) currentActiveTextureUnit = unit;
					if (c) c.activeTexture(texture);
				},
				glTexParameteri(target, pname, param) {
					const c = getGl();
					const boundId = getBoundTextureId(target);
					if (boundId && textureMeta[boundId]) {
						textureMeta[boundId].target = target;
						textureMeta[boundId].params.push([pname, param]);
					}
					if (!c) return;
					if (pname === 0x84FE) {
						if (anisoExt) c.texParameterf(target, 0x84FE, Math.min(maxAniso, Math.max(1, param)));
					} else {
						c.texParameteri(target, pname, param);
					}
				},
				glPixelStorei(pname, param) {
					const c = getGl();
					if (pname === 0x0CF5) currentUnpackAlignment = param | 0;
					if (c) c.pixelStorei(pname, param);
				},
				glTexImage2D(target, level, internalformat, width, height, border, format, type, pixelsPtr) {
					if (perfEnabled) perfStats.texUploads++;
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory) return;
					const ptr = pixelsPtr >>> 0;
					const boundId = getBoundTextureId(target);
					const tm = boundId ? textureMeta[boundId] : null;
					const lvl = level | 0;
					if (!ptr) {
						if (tm) {
							tm.target = target;
							if (lvl === 0) {
								tm.level = 0; tm.internalformat = internalformat;
								tm.width = width; tm.height = height; tm.border = border; tm.format = format; tm.type = type;
								tm.unpackAlignment = currentUnpackAlignment;
								tm.pixels = null;
							}
							tm.mips[lvl] = { target, level: lvl, internalformat, width, height, border, format, type, unpackAlignment: currentUnpackAlignment, pixels: null };
						}
						c.texImage2D(target, lvl, internalformat, width, height, border, format, type, null);
						return;
					}
					const count = width * height;
					if (type === 0x1402) {
						const view = new Int16Array(exports.memory.buffer, ptr, count);
						if (tm) {
							const copy = new Int16Array(view);
							tm.target = target;
							if (lvl === 0) {
								tm.level = 0; tm.internalformat = internalformat;
								tm.width = width; tm.height = height; tm.border = border; tm.format = format; tm.type = type;
								tm.unpackAlignment = currentUnpackAlignment;
								tm.pixels = copy;
							}
							tm.mips[lvl] = { target, level: lvl, internalformat, width, height, border, format, type, unpackAlignment: currentUnpackAlignment, pixels: copy };
						}
						c.texImage2D(target, lvl, internalformat, width, height, border, format, type, view);
					} else if (type === 0x1406) {
						const ch = (format === 0x1903 || format === 0x1902 || format === 0x8D94) ? 1 : 4;
						const view = new Float32Array(exports.memory.buffer, ptr, count * ch);
						if (tm) {
							const copy = new Float32Array(view);
							tm.target = target;
							if (lvl === 0) {
								tm.level = 0; tm.internalformat = internalformat;
								tm.width = width; tm.height = height; tm.border = border; tm.format = format; tm.type = type;
								tm.unpackAlignment = currentUnpackAlignment;
								tm.pixels = copy;
							}
							tm.mips[lvl] = { target, level: lvl, internalformat, width, height, border, format, type, unpackAlignment: currentUnpackAlignment, pixels: copy };
						}
						c.texImage2D(target, lvl, internalformat, width, height, border, format, type, view);
					} else {
						const bpp = (format === 0x1903 || format === 0x8D94 || internalformat === 0x8229) ? 1 : (format === 0x1907 ? 3 : 4);
						const byteLen = count * bpp;
						const view = new Uint8Array(exports.memory.buffer, ptr, byteLen);
						if (tm) {
							const copy = new Uint8Array(view);
							tm.target = target;
							if (lvl === 0) {
								tm.level = 0; tm.internalformat = internalformat;
								tm.width = width; tm.height = height; tm.border = border; tm.format = format; tm.type = type;
								tm.unpackAlignment = currentUnpackAlignment;
								tm.pixels = copy;
							}
							tm.mips[lvl] = { target, level: lvl, internalformat, width, height, border, format, type, unpackAlignment: currentUnpackAlignment, pixels: copy };
						}
						c.texImage2D(target, lvl, internalformat, width, height, border, format, type, view);
					}
				},
				glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixelsPtr) {
					if (perfEnabled) perfStats.texUploads++;
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory || !pixelsPtr) return;
					const ptr = pixelsPtr >>> 0;
					const lvl = level | 0;
					const count = width * height;
					const boundId = getBoundTextureId(target);
					const tm = boundId ? textureMeta[boundId] : null;
					const m = (tm && tm.mips) ? tm.mips[lvl] : null;
					ensureMemViews(exports.memory);
					if (type === 0x1402) {
						const view = ((ptr & 1) === 0)
							? memI16.subarray(ptr >>> 1, (ptr >>> 1) + count)
							: new Int16Array(exports.memory.buffer.slice(ptr, ptr + count * 2));
						if (m && m.pixels && xoffset === 0 && width === m.width && yoffset >= 0 && yoffset + height <= m.height) {
							m.pixels.set(view, yoffset * m.width);
						}
						c.texSubImage2D(target, lvl, xoffset, yoffset, width, height, format, type, view);
					} else {
						const bpp = (format === 0x1903 || format === 0x8D94) ? 1 : (format === 0x1907 ? 3 : 4);
						const byteLen = count * bpp;
						const view = memU8.subarray(ptr, ptr + byteLen);
						if (m && m.pixels && xoffset === 0 && width === m.width && yoffset >= 0 && yoffset + height <= m.height) {
							m.pixels.set(view, yoffset * m.width * bpp);
						}
						c.texSubImage2D(target, lvl, xoffset, yoffset, width, height, format, type, view);
					}
				},
				glTexImage3D(target, level, internalformat, width, height, depth, border, format, type, pixels) {
					if (perfEnabled) perfStats.texUploads++;
					const c = getGl();
					const exports = getWasm();
					if (!c || typeof c.texImage3D !== 'function') return;
					const ptr = pixels >>> 0;
					if (ptr === 0 || !exports || !exports.memory) {
						c.texImage3D(target, level, internalformat, width, height, depth, border, format, type, null);
						return;
					}
					if (type === 0x1402) {
						let view;
						if ((ptr & 1) === 0) {
							view = new Int16Array(exports.memory.buffer, ptr, width * height * depth);
						} else {
							view = new Int16Array(exports.memory.buffer.slice(ptr, ptr + width * height * depth * 2));
						}
						c.texImage3D(target, level, internalformat, width, height, depth, border, format, type, view);
					} else {
						const view = new Uint8Array(exports.memory.buffer, ptr, width * height * depth * 4);
						c.texImage3D(target, level, internalformat, width, height, depth, border, format, type, view);
					}
				},
				glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels) {
					if (perfEnabled) perfStats.texUploads++;
					const c = getGl();
					const exports = getWasm();
					if (!c || typeof c.texSubImage3D !== 'function' || !exports || !exports.memory || !pixels) return;
					const ptr = pixels >>> 0;
					if (type === 0x1402) {
						let view;
						if ((ptr & 1) === 0) {
							view = new Int16Array(exports.memory.buffer, ptr, width * height * depth);
						} else {
							view = new Int16Array(exports.memory.buffer.slice(ptr, ptr + width * height * depth * 2));
						}
						c.texSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, view);
					} else {
						const view = new Uint8Array(exports.memory.buffer, ptr, width * height * depth * 4);
						c.texSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, view);
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
					if (perfEnabled) perfStats.draws++;
					const c = getGl();
					if (c) {
						c.drawArrays(mode, first, count);
						if (glCanvas) glCanvas.__stele_dirty = true;
					}
				},
				glDrawElements(mode, count, type, offset) {
					if (perfEnabled) perfStats.draws++;
					const c = getGl();
					if (c) {
						c.drawElements(mode, count, type, offset >>> 0);
						if (glCanvas) glCanvas.__stele_dirty = true;
					}
				},
				glDrawElementsInstanced(mode, count, type, offset, instanceCount) {
					if (perfEnabled) perfStats.draws++;
					const c = getGl();
					if (c && typeof c.drawElementsInstanced === 'function') {
						c.drawElementsInstanced(mode, count, type, offset >>> 0, instanceCount);
						if (glCanvas) glCanvas.__stele_dirty = true;
					}
				},
				glDrawArraysInstanced(mode, first, count, instanceCount) {
					if (perfEnabled) perfStats.draws++;
					const c = getGl();
					if (c) {
						c.drawArraysInstanced(mode, first, count, instanceCount);
						if (glCanvas) glCanvas.__stele_dirty = true;
					}
				},
				glVertexAttribDivisor(index, divisor) {
					const c = getGl();
					if (c) c.vertexAttribDivisor(index, divisor);
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
					if (perfEnabled) perfStats.getUniformLocationCalls++;
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
					if (perfEnabled) perfStats.uniformUploads++;
					const c = getGl();
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (c && loc) c.uniform1i(loc, v0);
				},
				glUniform1f(locId, v0) {
					if (perfEnabled) perfStats.uniformUploads++;
					const c = getGl();
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (c && loc) c.uniform1f(loc, v0);
				},
				glUniform2f(locId, v0, v1) {
					if (perfEnabled) perfStats.uniformUploads++;
					const c = getGl();
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (c && loc) c.uniform2f(loc, v0, v1);
				},
				glUniform3f(locId, v0, v1, v2) {
					if (perfEnabled) perfStats.uniformUploads++;
					const c = getGl();
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (c && loc) c.uniform3f(loc, v0, v1, v2);
				},
				glUniform4f(locId, v0, v1, v2, v3) {
					if (perfEnabled) perfStats.uniformUploads++;
					const c = getGl();
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (c && loc) c.uniform4f(loc, v0, v1, v2, v3);
				},
				glUniform2fv(locId, count, valPtr) {
					if (perfEnabled) perfStats.uniformUploads++;
					if (count <= 0) return;
					const c = getGl();
					const exports = getWasm();
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (!c || !loc || !exports || !exports.memory) return;
					const ptr = valPtr >>> 0;
					const len = count * 2;
					if ((ptr & 3) === 0) {
						ensureMemViews(exports.memory);
						const off = ptr >>> 2;
						if (isWebGL2) {
							c.uniform2fv(loc, memF32, off, len);
						} else {
							c.uniform2fv(loc, memF32.subarray(off, off + len));
						}
					} else {
						c.uniform2fv(loc, new Float32Array(exports.memory.buffer, ptr, len));
					}
				},
				glUniform3fv(locId, count, valPtr) {
					if (perfEnabled) perfStats.uniformUploads++;
					if (count <= 0) return;
					const c = getGl();
					const exports = getWasm();
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (!c || !loc || !exports || !exports.memory) return;
					const ptr = valPtr >>> 0;
					const len = count * 3;
					if ((ptr & 3) === 0) {
						ensureMemViews(exports.memory);
						const off = ptr >>> 2;
						if (isWebGL2) {
							c.uniform3fv(loc, memF32, off, len);
						} else {
							c.uniform3fv(loc, memF32.subarray(off, off + len));
						}
					} else {
						c.uniform3fv(loc, new Float32Array(exports.memory.buffer, ptr, len));
					}
				},
				glUniformMatrix4fv(locId, count, trans, valPtr) {
					if (perfEnabled) perfStats.uniformUploads++;
					if (count <= 0) return;
					const c = getGl();
					const exports = getWasm();
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (!c || !loc || !exports || !exports.memory) return;
					const ptr = valPtr >>> 0;
					const len = count * 16;
					if ((ptr & 3) === 0) {
						ensureMemViews(exports.memory);
						const off = ptr >>> 2;
						if (isWebGL2) {
							c.uniformMatrix4fv(loc, !!trans, memF32, off, len);
						} else {
							c.uniformMatrix4fv(loc, !!trans, memF32.subarray(off, off + len));
						}
					} else {
						c.uniformMatrix4fv(loc, !!trans, new Float32Array(exports.memory.buffer, ptr, len));
					}
				},
				glDeleteShader(shaderId) {
					const c = getGl();
					const s = shaders[shaderId];
					if (shaderMeta[shaderId]) shaderMeta[shaderId].deleted = true;
					const w = (!parallelCompileExt) ? getShaderWorker() : null;
					if (!w) {
						if (c && s) c.deleteShader(s);
						shaders[shaderId] = null;
					}
				},
				glDeleteProgram(programId) {
					const c = getGl();
					const p = programs[programId];
					if (c && p) c.deleteProgram(p);
					if (currentProgramId === programId) currentProgramId = 0;
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
					if (perfEnabled) perfStats.blits++;
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
