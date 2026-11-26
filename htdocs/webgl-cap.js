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
	const vaos = [null];
	const vaoMeta = [null];
	let currentBoundVaoId = 0;
	let currentBoundFramebufferId = 0;
	let currentBoundRenderbufferId = 0;
	let currentProgramId = 0;
	let currentUnpackAlignment = 4;

	function getVaoAttrib(vm, idx) {
		let a = vm.attribs[idx];
		if (!a) {
			a = { enabled: false, hasPtr: false, bufId: 0, size: 4, type: 0x1406, normalized: false, stride: 0, offset: 0, divisor: 0 };
			vm.attribs[idx] = a;
		}
		return a;
	}
	const attachedShadersByProgram = new WeakMap();
	const uniformLocs = [null];
	const uniformMeta = [null];
	const globalScope = typeof window !== 'undefined' ? window : globalThis;

	let shaderWorker = null;
	let shaderWorkerUnavailable = false;
	let compileRevCounter = 1;

	/* Optional runtime bridge profiler (disabled by default; zero allocations in hot path) */
	let perfEnabled = !!(globalScope && globalScope.__webglPerfEnabled);
	let timerExt = null;
	let timerActiveQuery = null;
	let timerActivePass = 0;
	const timerQueryPool = [];
	const timerInFlight = [];
	const gpuPassStats = new Map();
	let gpuTimerDisjoints = 0;

	function resetGpuTimerQueries() {
		timerActiveQuery = null;
		timerActivePass = 0;
		timerQueryPool.length = 0;
		timerInFlight.length = 0;
	}

	function pollGpuTimers(c) {
		if (!c || !timerExt || timerInFlight.length === 0) return;
		const disjoint = !!c.getParameter(timerExt.GPU_DISJOINT_EXT);
		if (disjoint) gpuTimerDisjoints++;
		while (timerInFlight.length > 0) {
			const item = timerInFlight[0];
			const avail = c.getQueryParameter(item.query, c.QUERY_RESULT_AVAILABLE);
			if (!avail) break;
			timerInFlight.shift();
			const ns = Number(c.getQueryParameter(item.query, c.QUERY_RESULT)) || 0;
			if (!disjoint) {
				let st = gpuPassStats.get(item.passId);
				if (!st) {
					st = { samples: 0, totalNs: 0, maxNs: 0, lastNs: 0 };
					gpuPassStats.set(item.passId, st);
				}
				st.samples++;
				st.totalNs += ns;
				if (ns > st.maxNs) st.maxNs = ns;
				st.lastNs = ns;
			}
			if (timerQueryPool.length < 64) {
				timerQueryPool.push(item.query);
			} else if (typeof c.deleteQuery === 'function') {
				c.deleteQuery(item.query);
			}
		}
	}

	const perfStats = {
		getGlCalls: 0,
		domQueries: 0,
		getUniformLocationCalls: 0,
		uniformUploads: 0,
		texUploads: 0,
		bufferUploads: 0,
		draws: 0,
		blits: 0,
		cmdFlushes: 0,
		cmdOps: 0
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
			perfStats.cmdFlushes = 0;
			perfStats.cmdOps = 0;
			gpuPassStats.clear();
			gpuTimerDisjoints = 0;
		},
		snapshot() {
			if (gl && timerExt) pollGpuTimers(gl);
			const gpuPasses = {};
			for (const [id, st] of gpuPassStats.entries()) {
				const avgNs = st.samples > 0 ? (st.totalNs / st.samples) : 0;
				gpuPasses[id] = {
					samples: st.samples,
					avgMs: avgNs * 1e-6,
					maxMs: st.maxNs * 1e-6,
					lastMs: st.lastNs * 1e-6,
					totalMs: st.totalNs * 1e-6
				};
			}
			return {
				enabled: perfEnabled,
				...perfStats,
				gpuTimerSupported: !!timerExt,
				gpuTimerDisjoints,
				gpuTimerInFlight: timerInFlight.length,
				gpuPasses,
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

	function refreshUniformLocsForProgram(c, programId, restoreCurrent = true) {
		const p = programs[programId];
		const pm = programMeta[programId];
		if (!c || !p || !pm || !pm.localLinkedInitiated) return;
		let hasReplay = false;
		for (let i = 1; i < uniformMeta.length; i++) {
			const um = uniformMeta[i];
			if (um && um.programId === programId) {
				uniformLocs[i] = c.getUniformLocation(p, um.name);
				if (um.kind !== 0 && uniformLocs[i]) hasReplay = true;
			}
		}
		if (pm.uboBindings && pm.uboBindings.length > 0 && typeof c.getUniformBlockIndex === 'function' && typeof c.uniformBlockBinding === 'function') {
			for (const [bname, binding] of pm.uboBindings) {
				const idx = c.getUniformBlockIndex(p, bname);
				if (idx !== 0xFFFFFFFF && idx !== undefined) {
					c.uniformBlockBinding(p, idx, binding);
				}
			}
		}
		if (hasReplay && (!pm.localReady || pm.localLinkedOk)) {
			c.useProgram(p);
			for (let i = 1; i < uniformMeta.length; i++) {
				const um = uniformMeta[i];
				if (!um || um.programId !== programId || um.kind === 0) continue;
				const loc = uniformLocs[i];
				if (!loc) continue;
				switch (um.kind) {
				case 1: c.uniform1i(loc, um.v0); break;
				case 2: c.uniform1f(loc, um.v0); break;
				case 3: c.uniform2f(loc, um.v0, um.v1); break;
				case 4: c.uniform3f(loc, um.v0, um.v1, um.v2); break;
				case 5: c.uniform4f(loc, um.v0, um.v1, um.v2, um.v3); break;
				default: break;
				}
			}
			if (restoreCurrent) {
				const curP = currentProgramId > 0 ? programs[currentProgramId] : null;
				const curPm = currentProgramId > 0 ? programMeta[currentProgramId] : null;
				c.useProgram((curP && curPm && curPm.localLinkedInitiated && (!curPm.localReady || curPm.localLinkedOk)) ? curP : null);
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
			return webglMatch || fitMatch || null;
		}
		return null;
	}

	let parallelCompileExt = undefined;
	let anisoExt = null;
	let maxAniso = 1;

	function finalizeLocalProgramLink(c, programId, restoreCurrent = true) {
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
		refreshUniformLocsForProgram(c, programId, restoreCurrent);
	}

	function rebuildContextResources(c) {
		resetGpuTimerQueries();
		for (let i = 1; i < shaderMeta.length; i++) {
			const sm = shaderMeta[i];
			if (!sm) { shaders[i] = null; continue; }
			const s = c.createShader(sm.type);
			if (s && sm.src) {
				c.shaderSource(s, sm.src);
			}
			shaders[i] = s;
		}
		for (let i = 1; i < programs.length; i++) {
			programs[i] = null;
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
					finalizeLocalProgramLink(c, i, false);
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
		if (typeof c.createVertexArray === 'function' && typeof c.bindVertexArray === 'function') {
			for (let i = 1; i < vaoMeta.length; i++) {
				const vm = vaoMeta[i];
				if (!vm) { vaos[i] = null; continue; }
				const v = c.createVertexArray();
				vaos[i] = v;
				if (!v) continue;
				c.bindVertexArray(v);
				if (vm.ebId > 0) {
					c.bindBuffer(c.ELEMENT_ARRAY_BUFFER, buffers[vm.ebId] || null);
				}
				for (let loc = 0; loc < vm.attribs.length; loc++) {
					const a = vm.attribs[loc];
					if (!a) continue;
					if (a.hasPtr) {
						c.bindBuffer(c.ARRAY_BUFFER, a.bufId > 0 ? (buffers[a.bufId] || null) : null);
						c.vertexAttribPointer(loc, a.size, a.type, a.normalized, a.stride, a.offset);
					}
					if (a.divisor && typeof c.vertexAttribDivisor === 'function') {
						c.vertexAttribDivisor(loc, a.divisor);
					}
					if (a.enabled) {
						c.enableVertexAttribArray(loc);
					}
				}
			}
			c.bindVertexArray(null);
		}
		const nullFboTexIds = new Set();
		if (typeof c.activeTexture === 'function') c.activeTexture(0x84C0);
		for (let i = 1; i < textureMeta.length; i++) {
			const tm = textureMeta[i];
			if (!tm) { textures[i] = null; continue; }
			const t = c.createTexture();
			if (t && tm.target) {
				c.bindTexture(tm.target, t);
				if (tm.storage3D && typeof c.texStorage3D === 'function') {
					const st = tm.storage3D;
					c.texStorage3D(tm.target, st.levels, st.internalformat, st.width, st.height, st.depth);
					if (tm.layers && typeof c.texSubImage3D === 'function') {
						for (const entry of tm.layers.values()) {
							c.pixelStorei(c.UNPACK_ALIGNMENT, entry.unpackAlignment || 1);
							c.texSubImage3D(tm.target, entry.level, entry.xoffset, entry.yoffset, entry.zoffset,
								entry.width, entry.height, entry.depth, entry.format, entry.type, entry.pixels);
						}
					}
				} else if (Array.isArray(tm.mips) && tm.mips.length > 0) {
					let hasPixels = false;
					for (let lvl = 0; lvl < tm.mips.length; lvl++) {
						const m = tm.mips[lvl];
						if (m && m.width > 0 && m.height > 0) {
							if (m.pixels) {
								hasPixels = true;
								c.pixelStorei(c.UNPACK_ALIGNMENT, m.unpackAlignment || 1);
							}
							if ((m.is3D || tm.is3D) && m.depth > 0 && typeof c.texImage3D === 'function') {
								c.texImage3D(m.target || tm.target, lvl, m.internalformat, m.width, m.height, m.depth, m.border, m.format, m.type, m.pixels);
							} else {
								c.texImage2D(m.target || tm.target, lvl, m.internalformat, m.width, m.height, m.border, m.format, m.type, m.pixels);
							}
						}
					}
					if (!hasPixels && tm.width > 0 && tm.height > 0 && !tm.is3D) {
						nullFboTexIds.add(i);
					}
				} else if (tm.width > 0 && tm.height > 0) {
					if (tm.pixels) c.pixelStorei(c.UNPACK_ALIGNMENT, tm.unpackAlignment || 1);
					else if (!tm.is3D) nullFboTexIds.add(i);
					if (tm.is3D && tm.depth > 0 && typeof c.texImage3D === 'function') {
						c.texImage3D(tm.target, tm.level, tm.internalformat, tm.width, tm.height, tm.depth, tm.border, tm.format, tm.type, tm.pixels);
					} else {
						c.texImage2D(tm.target, tm.level, tm.internalformat, tm.width, tm.height, tm.border, tm.format, tm.type, tm.pixels);
					}
				}
				for (const [pname, param] of tm.params) {
					if (pname === 0x84FE) {
						if (anisoExt) c.texParameterf(tm.target, 0x84FE, Math.min(maxAniso, Math.max(1, param)));
					} else {
						c.texParameteri(tm.target, pname, param);
					}
				}
				if (tm.mipmapped && typeof c.generateMipmap === 'function') {
					c.generateMipmap(tm.target);
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
				const id2dArr = boundTextures2DArray[u] || 0;
				const id3d = boundTextures3D[u] || 0;
				if (u === 0 || id2d > 0) {
					c.activeTexture(0x84C0 + u);
					c.bindTexture(0x0DE1, id2d > 0 ? (textures[id2d] || null) : null);
				}
				if (idCube > 0) {
					c.activeTexture(0x84C0 + u);
					c.bindTexture(0x8513, textures[idCube] || null);
				}
				if (id2dArr > 0) {
					c.activeTexture(0x84C0 + u);
					c.bindTexture(0x8C1A, textures[id2dArr] || null);
				}
				if (id3d > 0) {
					c.activeTexture(0x84C0 + u);
					c.bindTexture(0x806F, textures[id3d] || null);
				}
			}
			c.activeTexture(0x84C0 + currentActiveTextureUnit);
		}
		if (typeof c.bindVertexArray === 'function') {
			c.bindVertexArray(currentBoundVaoId > 0 ? (vaos[currentBoundVaoId] || null) : null);
		}
		c.bindBuffer(c.ARRAY_BUFFER, currentBoundArrayBufferId > 0 ? (buffers[currentBoundArrayBufferId] || null) : null);
		if (currentBoundVaoId === 0) {
			c.bindBuffer(c.ELEMENT_ARRAY_BUFFER, currentBoundElementBufferId > 0 ? (buffers[currentBoundElementBufferId] || null) : null);
		}
		if (typeof c.bindBufferBase === 'function') {
			if (currentBoundUniformBufferId > 0) {
				c.bindBuffer(0x8A11, buffers[currentBoundUniformBufferId] || null);
			}
			for (const [idx, bid] of boundUniformBufferBases.entries()) {
				c.bindBufferBase(0x8A11, idx, bid > 0 ? (buffers[bid] || null) : null);
			}
		}
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
	let memU16 = null;
	let memI32 = null;
	let memU32 = null;
	let memF32 = null;

	function ensureMemViews(memory) {
		const buf = memory.buffer;
		if (buf !== cachedBuf) {
			cachedBuf = buf;
			memU8 = new Uint8Array(buf);
			memI16 = new Int16Array(buf);
			memU16 = new Uint16Array(buf);
			memI32 = new Int32Array(buf);
			memU32 = new Uint32Array(buf);
			memF32 = new Float32Array(buf);
		}
	}

	function getGl(forceCheck) {
		if (perfEnabled) perfStats.getGlCalls++;
		const curWGen = globalScope.__stele_worker_canvases_gen || 0;
		if (!forceCheck && gl && glCanvas &&
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
				timerExt = gl.getExtension('EXT_disjoint_timer_query_webgl2') || null;
			} else {
				timerExt = null;
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
	let currentBoundUniformBufferId = 0;
	const boundUniformBufferBases = new Map();
	let currentActiveTextureUnit = 0;
	const boundTextures2D = new Array(32).fill(0);
	const boundTexturesCube = new Array(32).fill(0);
	const boundTextures2DArray = new Array(32).fill(0);
	const boundTextures3D = new Array(32).fill(0);

	function boundTextureMap(target) {
		if (target === 0x8513) return boundTexturesCube;
		if (target === 0x8C1A) return boundTextures2DArray;
		if (target === 0x806F) return boundTextures3D;
		return boundTextures2D;
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
			memU16 = null;
			memI32 = null;
			memU32 = null;
			memF32 = null;
			parallelCompileExt = undefined;
			anisoExt = null;
			maxAniso = 1;
			timerExt = null;
			resetGpuTimerQueries();
			currentProgramId = 0;
			currentBoundVaoId = 0;
			currentBoundUniformBufferId = 0;
			boundUniformBufferBases.clear();
			currentUnpackAlignment = 4;
			currentActiveTextureUnit = 0;
			boundTextures2D.fill(0);
			boundTexturesCube.fill(0);
			boundTextures2DArray.fill(0);
			boundTextures3D.fill(0);
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
						uboBlockNames: new Map(),
						uboBindings: [],
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
					else if (target === 0x8893) {
						if (currentBoundVaoId > 0 && vaoMeta[currentBoundVaoId]) {
							vaoMeta[currentBoundVaoId].ebId = bufferId;
						} else {
							currentBoundElementBufferId = bufferId;
						}
					} else if (target === 0x8A11) currentBoundUniformBufferId = bufferId;
					const b = buffers[bufferId];
					if (c) c.bindBuffer(target, b || null);
				},
				glBufferData(target, dataPtr, size, usage) {
					if (perfEnabled) perfStats.bufferUploads++;
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory) return;
					const curEb = (currentBoundVaoId > 0 && vaoMeta[currentBoundVaoId]) ? vaoMeta[currentBoundVaoId].ebId : currentBoundElementBufferId;
					const bid = target === 0x8892 ? currentBoundArrayBufferId : (target === 0x8893 ? curEb : (target === 0x8A11 ? currentBoundUniformBufferId : 0));
					const uptr = dataPtr >>> 0;
					const usize = size >>> 0;
					if (!uptr) {
						if (bid && bufferMeta[bid]) {
							bufferMeta[bid].target = target;
							bufferMeta[bid].data = (usage === 0x88E4 && usize > 0) ? new Uint8Array(usize) : null;
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
					const curEb = (currentBoundVaoId > 0 && vaoMeta[currentBoundVaoId]) ? vaoMeta[currentBoundVaoId].ebId : currentBoundElementBufferId;
					const bid = target === 0x8892 ? currentBoundArrayBufferId : (target === 0x8893 ? curEb : (target === 0x8A11 ? currentBoundUniformBufferId : 0));
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
					textureMeta.push({ target: 0, is3D: false, storage3D: null, layers: null, mipmapped: false, params: [], mips: [], level: 0, internalformat: 0, width: 0, height: 0, depth: 0, border: 0, format: 0, type: 0, unpackAlignment: 4, pixels: null });
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
						if (boundTextures2DArray[u] === texId) boundTextures2DArray[u] = 0;
						if (boundTextures3D[u] === texId) boundTextures3D[u] = 0;
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
						const tm = textureMeta[boundId];
						tm.target = target;
						let replaced = false;
						for (let i = 0; i < tm.params.length; i++) {
							if (tm.params[i][0] === pname) {
								tm.params[i][1] = param;
								replaced = true;
								break;
							}
						}
						if (!replaced) tm.params.push([pname, param]);
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
					ensureMemViews(exports.memory);
					const count = width * height;
					let view;
					let CopyCtor;
					if (type === 0x1402) {
						view = ((ptr & 1) === 0)
							? memI16.subarray(ptr >>> 1, (ptr >>> 1) + count)
							: new Int16Array(exports.memory.buffer.slice(ptr, ptr + count * 2));
						CopyCtor = Int16Array;
					} else if (type === 0x1406) {
						const ch = (format === 0x1903 || format === 0x1902 || format === 0x8D94) ? 1 : 4;
						const len = count * ch;
						view = ((ptr & 3) === 0)
							? memF32.subarray(ptr >>> 2, (ptr >>> 2) + len)
							: new Float32Array(exports.memory.buffer.slice(ptr, ptr + len * 4));
						CopyCtor = Float32Array;
					} else {
						const bpp = (format === 0x1903 || format === 0x8D94 || internalformat === 0x8229) ? 1 : (format === 0x1907 ? 3 : 4);
						const byteLen = count * bpp;
						view = memU8.subarray(ptr, ptr + byteLen);
						CopyCtor = Uint8Array;
					}
					if (tm) {
						const prevMip = tm.mips[lvl];
						let copy;
						if (prevMip && prevMip.pixels && prevMip.pixels instanceof CopyCtor && prevMip.pixels.length === view.length) {
							copy = prevMip.pixels;
							copy.set(view);
							prevMip.target = target;
							prevMip.internalformat = internalformat;
							prevMip.width = width;
							prevMip.height = height;
							prevMip.border = border;
							prevMip.format = format;
							prevMip.type = type;
							prevMip.unpackAlignment = currentUnpackAlignment;
						} else {
							copy = new CopyCtor(view);
							tm.mips[lvl] = { target, level: lvl, internalformat, width, height, border, format, type, unpackAlignment: currentUnpackAlignment, pixels: copy };
						}
						tm.target = target;
						if (lvl === 0) {
							tm.level = 0; tm.internalformat = internalformat;
							tm.width = width; tm.height = height; tm.border = border; tm.format = format; tm.type = type;
							tm.unpackAlignment = currentUnpackAlignment;
							tm.pixels = copy;
						}
					}
					c.texImage2D(target, lvl, internalformat, width, height, border, format, type, view);
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
					} else if (type === 0x1406) {
						const ch = (format === 0x1903 || format === 0x1902 || format === 0x8D94) ? 1 : 4;
						const len = count * ch;
						const view = ((ptr & 3) === 0)
							? memF32.subarray(ptr >>> 2, (ptr >>> 2) + len)
							: new Float32Array(exports.memory.buffer.slice(ptr, ptr + len * 4));
						if (m && m.pixels && xoffset === 0 && width === m.width && yoffset >= 0 && yoffset + height <= m.height) {
							m.pixels.set(view, yoffset * m.width * ch);
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
					const lvl = level | 0;
					const boundId = getBoundTextureId(target);
					const tm = boundId ? textureMeta[boundId] : null;
					if (ptr === 0 || !exports || !exports.memory) {
						if (tm) {
							tm.target = target;
							tm.is3D = true;
							if (lvl === 0) {
								tm.level = 0; tm.internalformat = internalformat;
								tm.width = width; tm.height = height; tm.depth = depth; tm.border = border;
								tm.format = format; tm.type = type;
								tm.unpackAlignment = currentUnpackAlignment;
								tm.pixels = null;
							}
							tm.mips[lvl] = { target, is3D: true, level: lvl, internalformat, width, height, depth, border, format, type, unpackAlignment: currentUnpackAlignment, pixels: null };
						}
						c.texImage3D(target, lvl, internalformat, width, height, depth, border, format, type, null);
						return;
					}
					ensureMemViews(exports.memory);
					const count = width * height * depth;
					let view;
					let CopyCtor;
					if (type === 0x1402) {
						view = ((ptr & 1) === 0)
							? memI16.subarray(ptr >>> 1, (ptr >>> 1) + count)
							: new Int16Array(exports.memory.buffer.slice(ptr, ptr + count * 2));
						CopyCtor = Int16Array;
					} else if (type === 0x1406) {
						const ch = (format === 0x1903 || format === 0x1902 || format === 0x8D94) ? 1 : 4;
						const len = count * ch;
						view = ((ptr & 3) === 0)
							? memF32.subarray(ptr >>> 2, (ptr >>> 2) + len)
							: new Float32Array(exports.memory.buffer.slice(ptr, ptr + len * 4));
						CopyCtor = Float32Array;
					} else {
						const bpp = (format === 0x1903 || format === 0x8D94 || internalformat === 0x8229) ? 1 : (format === 0x1907 ? 3 : 4);
						view = memU8.subarray(ptr, ptr + count * bpp);
						CopyCtor = Uint8Array;
					}
					if (tm) {
						const prevMip = tm.mips[lvl];
						let copy;
						if (prevMip && prevMip.pixels && prevMip.pixels instanceof CopyCtor && prevMip.pixels.length === view.length) {
							copy = prevMip.pixels;
							copy.set(view);
							prevMip.target = target;
							prevMip.is3D = true;
							prevMip.internalformat = internalformat;
							prevMip.width = width;
							prevMip.height = height;
							prevMip.depth = depth;
							prevMip.border = border;
							prevMip.format = format;
							prevMip.type = type;
							prevMip.unpackAlignment = currentUnpackAlignment;
						} else {
							copy = new CopyCtor(view);
							tm.mips[lvl] = { target, is3D: true, level: lvl, internalformat, width, height, depth, border, format, type, unpackAlignment: currentUnpackAlignment, pixels: copy };
						}
						tm.target = target;
						tm.is3D = true;
						if (lvl === 0) {
							tm.level = 0; tm.internalformat = internalformat;
							tm.width = width; tm.height = height; tm.depth = depth; tm.border = border;
							tm.format = format; tm.type = type;
							tm.unpackAlignment = currentUnpackAlignment;
							tm.pixels = copy;
						}
					}
					c.texImage3D(target, lvl, internalformat, width, height, depth, border, format, type, view);
				},
				glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels) {
					if (perfEnabled) perfStats.texUploads++;
					const c = getGl();
					const exports = getWasm();
					if (!c || typeof c.texSubImage3D !== 'function' || !exports || !exports.memory || !pixels) return;
					const ptr = pixels >>> 0;
					const lvl = level | 0;
					const boundId = getBoundTextureId(target);
					const tm = boundId ? textureMeta[boundId] : null;
					const m = (tm && tm.mips) ? tm.mips[lvl] : null;
					ensureMemViews(exports.memory);
					const count = width * height * depth;
					let view;
					let CopyCtor;
					if (type === 0x1402) {
						view = ((ptr & 1) === 0)
							? memI16.subarray(ptr >>> 1, (ptr >>> 1) + count)
							: new Int16Array(exports.memory.buffer.slice(ptr, ptr + count * 2));
						CopyCtor = Int16Array;
						if (m && m.pixels && xoffset === 0 && yoffset === 0 && width === m.width && height === m.height && zoffset >= 0 && zoffset + depth <= m.depth) {
							m.pixels.set(view, zoffset * m.width * m.height);
						}
					} else if (type === 0x140B) {
						const ch = (format === 0x1903 || format === 0x1902 || format === 0x8D94) ? 1 : 4;
						const len = count * ch;
						view = ((ptr & 1) === 0)
							? memU16.subarray(ptr >>> 1, (ptr >>> 1) + len)
							: new Uint16Array(exports.memory.buffer.slice(ptr, ptr + len * 2));
						CopyCtor = Uint16Array;
						if (m && m.pixels && xoffset === 0 && yoffset === 0 && width === m.width && height === m.height && zoffset >= 0 && zoffset + depth <= m.depth) {
							m.pixels.set(view, zoffset * m.width * m.height * ch);
						}
					} else if (type === 0x1406) {
						const ch = (format === 0x1903 || format === 0x1902 || format === 0x8D94) ? 1 : 4;
						const len = count * ch;
						view = ((ptr & 3) === 0)
							? memF32.subarray(ptr >>> 2, (ptr >>> 2) + len)
							: new Float32Array(exports.memory.buffer.slice(ptr, ptr + len * 4));
						CopyCtor = Float32Array;
						if (m && m.pixels && xoffset === 0 && yoffset === 0 && width === m.width && height === m.height && zoffset >= 0 && zoffset + depth <= m.depth) {
							m.pixels.set(view, zoffset * m.width * m.height * ch);
						}
					} else {
						const bpp = (format === 0x1903 || format === 0x8D94) ? 1 : (format === 0x1907 ? 3 : 4);
						view = memU8.subarray(ptr, ptr + count * bpp);
						CopyCtor = Uint8Array;
						if (m && m.pixels && xoffset === 0 && yoffset === 0 && width === m.width && height === m.height && zoffset >= 0 && zoffset + depth <= m.depth) {
							m.pixels.set(view, zoffset * m.width * m.height * bpp);
						}
					}
					if (tm && tm.storage3D && tm.layers && depth === 1) {
						const key = (lvl << 16) | (zoffset & 0xFFFF);
						const prev = tm.layers.get(key);
						if (prev && prev.pixels instanceof CopyCtor && prev.pixels.length === view.length &&
						    prev.xoffset === xoffset && prev.yoffset === yoffset && prev.width === width && prev.height === height) {
							prev.pixels.set(view);
							prev.format = format;
							prev.type = type;
							prev.unpackAlignment = currentUnpackAlignment;
						} else {
							tm.layers.set(key, {
								level: lvl, xoffset, yoffset, zoffset, width, height, depth,
								format, type, unpackAlignment: currentUnpackAlignment,
								pixels: new CopyCtor(view)
							});
						}
					}
					c.texSubImage3D(target, lvl, xoffset, yoffset, zoffset, width, height, depth, format, type, view);
				},
				glTexStorage3D(target, levels, internalformat, width, height, depth) {
					const c = getGl();
					const boundId = getBoundTextureId(target);
					const tm = boundId ? textureMeta[boundId] : null;
					if (tm) {
						tm.target = target;
						tm.is3D = true;
						tm.storage3D = { levels: levels | 0, internalformat, width, height, depth };
						tm.width = width;
						tm.height = height;
						tm.depth = depth;
						tm.internalformat = internalformat;
						tm.mips = [];
						tm.layers = new Map();
						tm.mipmapped = false;
					}
					if (c && typeof c.texStorage3D === 'function') {
						c.texStorage3D(target, levels | 0, internalformat, width, height, depth);
					}
				},
				glGenerateMipmap(target) {
					const c = getGl();
					const boundId = getBoundTextureId(target);
					const tm = boundId ? textureMeta[boundId] : null;
					if (tm) tm.mipmapped = true;
					if (c && typeof c.generateMipmap === 'function') {
						c.generateMipmap(target);
					}
				},
				glCreateVertexArray() {
					const c = getGl();
					if (!c || typeof c.createVertexArray !== 'function') return 0;
					const v = c.createVertexArray();
					if (!v) return 0;
					vaos.push(v);
					vaoMeta.push({ ebId: 0, attribs: [] });
					return vaos.length - 1;
				},
				glDeleteVertexArray(vaoId) {
					const c = getGl();
					const v = vaos[vaoId];
					if (c && v && typeof c.deleteVertexArray === 'function') c.deleteVertexArray(v);
					if (vaoId > 0 && vaoId < vaos.length) {
						vaos[vaoId] = null;
						vaoMeta[vaoId] = null;
					}
					if (currentBoundVaoId === vaoId) currentBoundVaoId = 0;
				},
				glBindVertexArray(vaoId) {
					const c = getGl();
					const vid = vaoId | 0;
					currentBoundVaoId = vid;
					if (c && typeof c.bindVertexArray === 'function') {
						c.bindVertexArray(vid > 0 ? (vaos[vid] || null) : null);
					}
				},
				glEnableVertexAttribArray(index) {
					const c = getGl();
					if (currentBoundVaoId > 0 && vaoMeta[currentBoundVaoId]) {
						getVaoAttrib(vaoMeta[currentBoundVaoId], index >>> 0).enabled = true;
					}
					if (c) c.enableVertexAttribArray(index);
				},
				glDisableVertexAttribArray(index) {
					const c = getGl();
					if (currentBoundVaoId > 0 && vaoMeta[currentBoundVaoId]) {
						getVaoAttrib(vaoMeta[currentBoundVaoId], index >>> 0).enabled = false;
					}
					if (c) c.disableVertexAttribArray(index);
				},
				glVertexAttribPointer(index, size, type, normalized, stride, offset) {
					const c = getGl();
					const off = offset >>> 0;
					if (currentBoundVaoId > 0 && vaoMeta[currentBoundVaoId]) {
						const a = getVaoAttrib(vaoMeta[currentBoundVaoId], index >>> 0);
						a.hasPtr = true;
						a.bufId = currentBoundArrayBufferId;
						a.size = size;
						a.type = type;
						a.normalized = !!normalized;
						a.stride = stride;
						a.offset = off;
					}
					if (c) c.vertexAttribPointer(index, size, type, !!normalized, stride, off);
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
					if (currentBoundVaoId > 0 && vaoMeta[currentBoundVaoId]) {
						getVaoAttrib(vaoMeta[currentBoundVaoId], index >>> 0).divisor = divisor >>> 0;
					}
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
					uniformMeta.push({ programId, name, kind: 0, v0: 0, v1: 0, v2: 0, v3: 0 });
					return uniformLocs.length - 1;
				},
				glUniform1i(locId, v0) {
					if (perfEnabled) perfStats.uniformUploads++;
					const c = getGl();
					const um = locId >= 0 ? uniformMeta[locId] : null;
					if (um) { um.kind = 1; um.v0 = v0 | 0; }
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (c && loc) c.uniform1i(loc, v0);
				},
				glUniform1f(locId, v0) {
					if (perfEnabled) perfStats.uniformUploads++;
					const c = getGl();
					const um = locId >= 0 ? uniformMeta[locId] : null;
					if (um) { um.kind = 2; um.v0 = v0; }
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (c && loc) c.uniform1f(loc, v0);
				},
				glUniform2f(locId, v0, v1) {
					if (perfEnabled) perfStats.uniformUploads++;
					const c = getGl();
					const um = locId >= 0 ? uniformMeta[locId] : null;
					if (um) { um.kind = 3; um.v0 = v0; um.v1 = v1; }
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (c && loc) c.uniform2f(loc, v0, v1);
				},
				glUniform3f(locId, v0, v1, v2) {
					if (perfEnabled) perfStats.uniformUploads++;
					const c = getGl();
					const um = locId >= 0 ? uniformMeta[locId] : null;
					if (um) { um.kind = 4; um.v0 = v0; um.v1 = v1; um.v2 = v2; }
					const loc = locId >= 0 ? uniformLocs[locId] : null;
					if (c && loc) c.uniform3f(loc, v0, v1, v2);
				},
				glUniform4f(locId, v0, v1, v2, v3) {
					if (perfEnabled) perfStats.uniformUploads++;
					const c = getGl();
					const um = locId >= 0 ? uniformMeta[locId] : null;
					if (um) { um.kind = 5; um.v0 = v0; um.v1 = v1; um.v2 = v2; um.v3 = v3; }
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
					if (bufferId > 0 && bufferId < buffers.length) {
						buffers[bufferId] = null;
						bufferMeta[bufferId] = null;
					}
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
				},
				glBindBufferBase(target, index, bufferId) {
					const c = getGl();
					const idx = index >>> 0;
					const bid = bufferId | 0;
					if (target === 0x8A11) {
						currentBoundUniformBufferId = bid;
						boundUniformBufferBases.set(idx, bid);
					}
					if (c && typeof c.bindBufferBase === 'function') {
						c.bindBufferBase(target, idx, bid > 0 ? (buffers[bid] || null) : null);
					}
				},
				glGetUniformBlockIndex(programId, namePtr, nameLen) {
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory || typeof c.getUniformBlockIndex !== 'function') return 0xFFFFFFFF;
					const p = programs[programId];
					if (!p) return 0xFFFFFFFF;
					const name = readStr(exports.memory, namePtr, nameLen);
					const idx = c.getUniformBlockIndex(p, name);
					if (idx !== 0xFFFFFFFF && idx !== undefined && programMeta[programId]) {
						programMeta[programId].uboBlockNames.set(idx >>> 0, name);
					}
					return (idx === undefined ? 0xFFFFFFFF : idx) >>> 0;
				},
				glUniformBlockBinding(programId, blockIndex, blockBinding) {
					const c = getGl();
					const p = programs[programId];
					const pm = programMeta[programId];
					const bidx = blockIndex >>> 0;
					const bind = blockBinding >>> 0;
					if (pm && pm.uboBlockNames) {
						const bname = pm.uboBlockNames.get(bidx);
						if (bname) {
							let found = false;
							for (let k = 0; k < pm.uboBindings.length; k++) {
								if (pm.uboBindings[k][0] === bname) {
									pm.uboBindings[k][1] = bind;
									found = true;
									break;
								}
							}
							if (!found) pm.uboBindings.push([bname, bind]);
						}
					}
					if (c && p && bidx !== 0xFFFFFFFF && typeof c.uniformBlockBinding === 'function') {
						c.uniformBlockBinding(p, bidx, bind);
					}
				},
				glCmdFlush(bufPtr, wordCount) {
					const n = wordCount >>> 0;
					if (!n) return;
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory) return;
					const ptr = bufPtr >>> 0;
					if (!ptr || (ptr & 3) !== 0) return;
					ensureMemViews(exports.memory);
					if (perfEnabled) perfStats.cmdFlushes++;
					const u32 = memU32;
					const i32 = memI32;
					const f32 = memF32;
					let pos = ptr >>> 2;
					const end = pos + n;
					let drew = false;
					let ops = 0;
					while (pos < end) {
						const op = u32[pos++];
						ops++;
						switch (op) {
						case 0: /* NOP */
							break;
						case 1: { /* USE_PROGRAM */
							const pid = i32[pos++];
							currentProgramId = pid;
							if (pid <= 0) {
								c.useProgram(null);
							} else {
								const p = programs[pid];
								const pm = programMeta[pid];
								if (p && pm && pm.linked && !pm.localLinkedInitiated) {
									if (!(pm.workerDone && !pm.workerOk)) {
										finalizeLocalProgramLink(c, pid);
									}
								}
								if (p && pm && pm.localReady && !pm.localLinkedOk) {
									c.useProgram(null);
								} else {
									c.useProgram(p || null);
								}
							}
							break;
						}
						case 2: { /* ACTIVE_TEXTURE */
							const texEnum = u32[pos++];
							const unit = (texEnum - 0x84C0) | 0;
							if (unit >= 0 && unit < 32) currentActiveTextureUnit = unit;
							c.activeTexture(texEnum);
							break;
						}
						case 3: { /* BIND_TEXTURE */
							const target = u32[pos++];
							const tid = u32[pos++];
							setBoundTextureId(target, tid);
							c.bindTexture(target, tid > 0 ? (textures[tid] || null) : null);
							break;
						}
						case 4: { /* BIND_BUFFER */
							const target = u32[pos++];
							const bid = u32[pos++];
							if (target === 0x8892) currentBoundArrayBufferId = bid;
							else if (target === 0x8893) {
								if (currentBoundVaoId > 0 && vaoMeta[currentBoundVaoId]) {
									vaoMeta[currentBoundVaoId].ebId = bid;
								} else {
									currentBoundElementBufferId = bid;
								}
							} else if (target === 0x8A11) currentBoundUniformBufferId = bid;
							c.bindBuffer(target, bid > 0 ? (buffers[bid] || null) : null);
							break;
						}
						case 5: /* ENABLE */
							c.enable(u32[pos++]);
							break;
						case 6: /* DISABLE */
							c.disable(u32[pos++]);
							break;
						case 7: /* DEPTH_MASK */
							c.depthMask(u32[pos++] !== 0);
							break;
						case 8: { /* BLEND_FUNC */
							const sf = u32[pos++];
							const df = u32[pos++];
							c.blendFunc(sf, df);
							break;
						}
						case 9: { /* UNIFORM1I */
							const locId = i32[pos++];
							const v0 = i32[pos++];
							const um = locId >= 0 ? uniformMeta[locId] : null;
							if (um) { um.kind = 1; um.v0 = v0 | 0; }
							const loc = locId >= 0 ? uniformLocs[locId] : null;
							if (loc) c.uniform1i(loc, v0);
							if (perfEnabled) perfStats.uniformUploads++;
							break;
						}
						case 10: { /* UNIFORM1F */
							const locId = i32[pos++];
							const v0 = f32[pos++];
							const um = locId >= 0 ? uniformMeta[locId] : null;
							if (um) { um.kind = 2; um.v0 = v0; }
							const loc = locId >= 0 ? uniformLocs[locId] : null;
							if (loc) c.uniform1f(loc, v0);
							if (perfEnabled) perfStats.uniformUploads++;
							break;
						}
						case 11: { /* UNIFORM2F */
							const locId = i32[pos++];
							const v0 = f32[pos++];
							const v1 = f32[pos++];
							const um = locId >= 0 ? uniformMeta[locId] : null;
							if (um) { um.kind = 3; um.v0 = v0; um.v1 = v1; }
							const loc = locId >= 0 ? uniformLocs[locId] : null;
							if (loc) c.uniform2f(loc, v0, v1);
							if (perfEnabled) perfStats.uniformUploads++;
							break;
						}
						case 12: { /* UNIFORM3F */
							const locId = i32[pos++];
							const v0 = f32[pos++];
							const v1 = f32[pos++];
							const v2 = f32[pos++];
							const um = locId >= 0 ? uniformMeta[locId] : null;
							if (um) { um.kind = 4; um.v0 = v0; um.v1 = v1; um.v2 = v2; }
							const loc = locId >= 0 ? uniformLocs[locId] : null;
							if (loc) c.uniform3f(loc, v0, v1, v2);
							if (perfEnabled) perfStats.uniformUploads++;
							break;
						}
						case 13: { /* UNIFORM4F */
							const locId = i32[pos++];
							const v0 = f32[pos++];
							const v1 = f32[pos++];
							const v2 = f32[pos++];
							const v3 = f32[pos++];
							const um = locId >= 0 ? uniformMeta[locId] : null;
							if (um) { um.kind = 5; um.v0 = v0; um.v1 = v1; um.v2 = v2; um.v3 = v3; }
							const loc = locId >= 0 ? uniformLocs[locId] : null;
							if (loc) c.uniform4f(loc, v0, v1, v2, v3);
							if (perfEnabled) perfStats.uniformUploads++;
							break;
						}
						case 14: { /* UNIFORM_MAT4 */
							const locId = i32[pos++];
							const trans = u32[pos++] !== 0;
							const loc = locId >= 0 ? uniformLocs[locId] : null;
							if (loc) {
								if (isWebGL2) {
									c.uniformMatrix4fv(loc, trans, f32, pos, 16);
								} else {
									c.uniformMatrix4fv(loc, trans, f32.subarray(pos, pos + 16));
								}
							}
							pos += 16;
							if (perfEnabled) perfStats.uniformUploads++;
							break;
						}
						case 15: { /* VERTEX_ATTRIB_PTR */
							const idx = u32[pos++];
							const size = i32[pos++];
							const type = u32[pos++];
							const norm = u32[pos++] !== 0;
							const stride = i32[pos++];
							const off = u32[pos++];
							if (currentBoundVaoId > 0 && vaoMeta[currentBoundVaoId]) {
								const a = getVaoAttrib(vaoMeta[currentBoundVaoId], idx);
								a.hasPtr = true;
								a.bufId = currentBoundArrayBufferId;
								a.size = size;
								a.type = type;
								a.normalized = norm;
								a.stride = stride;
								a.offset = off;
							}
							c.vertexAttribPointer(idx, size, type, norm, stride, off);
							break;
						}
						case 16: { /* DRAW_ELEMENTS */
							const mode = u32[pos++];
							const count = i32[pos++];
							const type = u32[pos++];
							const off = u32[pos++];
							c.drawElements(mode, count, type, off);
							drew = true;
							if (perfEnabled) perfStats.draws++;
							break;
						}
						case 17: { /* DRAW_ELEMENTS_INSTANCED */
							const mode = u32[pos++];
							const count = i32[pos++];
							const type = u32[pos++];
							const off = u32[pos++];
							const inst = i32[pos++];
							if (typeof c.drawElementsInstanced === 'function') {
								c.drawElementsInstanced(mode, count, type, off, inst);
							}
							drew = true;
							if (perfEnabled) perfStats.draws++;
							break;
						}
						case 18: { /* DRAW_ARRAYS */
							const mode = u32[pos++];
							const first = i32[pos++];
							const count = i32[pos++];
							c.drawArrays(mode, first, count);
							drew = true;
							if (perfEnabled) perfStats.draws++;
							break;
						}
						case 19: { /* ENABLE_VAA */
							const idx = u32[pos++];
							if (currentBoundVaoId > 0 && vaoMeta[currentBoundVaoId]) {
								getVaoAttrib(vaoMeta[currentBoundVaoId], idx).enabled = true;
							}
							c.enableVertexAttribArray(idx);
							break;
						}
						case 20: { /* DISABLE_VAA */
							const idx = u32[pos++];
							if (currentBoundVaoId > 0 && vaoMeta[currentBoundVaoId]) {
								getVaoAttrib(vaoMeta[currentBoundVaoId], idx).enabled = false;
							}
							c.disableVertexAttribArray(idx);
							break;
						}
						case 21: { /* VERTEX_ATTRIB_DIVISOR */
							const idx = u32[pos++];
							const div = u32[pos++];
							if (currentBoundVaoId > 0 && vaoMeta[currentBoundVaoId]) {
								getVaoAttrib(vaoMeta[currentBoundVaoId], idx).divisor = div;
							}
							if (typeof c.vertexAttribDivisor === 'function') {
								c.vertexAttribDivisor(idx, div);
							}
							break;
						}
						case 22: { /* BIND_VAO */
							const vid = i32[pos++];
							currentBoundVaoId = vid;
							if (typeof c.bindVertexArray === 'function') {
								c.bindVertexArray(vid > 0 ? (vaos[vid] || null) : null);
							}
							break;
						}
						default:
							pos = end;
							break;
						}
					}
					if (perfEnabled) perfStats.cmdOps += ops;
					if (drew && glCanvas) glCanvas.__stele_dirty = true;
				},
				glTimerBegin(passId) {
					if (!perfEnabled || !timerExt || timerActiveQuery) return;
					const c = getGl();
					if (!c || typeof c.createQuery !== 'function') return;
					pollGpuTimers(c);
					const q = timerQueryPool.length > 0 ? timerQueryPool.pop() : c.createQuery();
					if (!q) return;
					c.beginQuery(timerExt.TIME_ELAPSED_EXT, q);
					timerActiveQuery = q;
					timerActivePass = passId >>> 0;
				},
				glTimerEnd() {
					if (!timerActiveQuery) return;
					const c = getGl();
					if (!c || !timerExt) {
						timerActiveQuery = null;
						timerActivePass = 0;
						return;
					}
					c.endQuery(timerExt.TIME_ELAPSED_EXT);
					timerInFlight.push({ passId: timerActivePass, query: timerActiveQuery });
					timerActiveQuery = null;
					timerActivePass = 0;
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
