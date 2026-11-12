/*
 * webgl-bridge.js — Browser WebAssembly bridge for WebGL 1/2 graphics.
 * Provides the "webgl" WASM import module.
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

export function createWebGlBridge(root, getWasm) {
	let gl = null;
	let glCanvas = null;
	let kicked = false;
	const shaders = [null];
	const programs = [null];
	const buffers = [null];
	const textures = [null];
	const uniformLocs = [null];

	function findCanvas() {
		const scope = (root && root.querySelector) ? root : (typeof document !== 'undefined' ? document : null);
		if (!scope) return null;
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

	let parallelCompileExt = undefined;

	function getGl() {
		if (gl && glCanvas && glCanvas.isConnected !== false) return gl;
		const canvas = findCanvas();
		if (canvas) {
			const glOpts = { preserveDrawingBuffer: true, alpha: true, antialias: true, depth: true };
			gl = canvas.getContext('webgl2', glOpts) || canvas.getContext('webgl', glOpts);
			glCanvas = canvas;
			parallelCompileExt = (gl && typeof gl.getExtension === 'function')
				? gl.getExtension('KHR_parallel_shader_compile')
				: null;
			if (!kicked && typeof requestAnimationFrame === 'function') {
				kicked = true;
				const fn = getExport(getWasm ? getWasm() : null, ['wasm_canvas_render']);
				if (fn) requestAnimationFrame(() => fn());
			}
		}
		return gl;
	}

	return {
		mount() {
			getGl();
		},
		destroy() {
			gl = null;
			glCanvas = null;
			parallelCompileExt = undefined;
		},
		makeImports() {
			return {
				glViewport(x, y, w, h) {
					const c = getGl();
					if (!c) return;
					if (glCanvas && glCanvas.hasAttribute && glCanvas.hasAttribute('data-bud-canvas-fit') && w > 0 && h > 0) {
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
					if (c) c.clear(mask);
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
				glCreateShader(type) {
					const c = getGl();
					if (!c) return 0;
					const s = c.createShader(type);
					shaders.push(s);
					return shaders.length - 1;
				},
				glShaderSource(shaderId, srcPtr, srcLen) {
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory) return;
					const src = readStr(exports.memory, srcPtr, srcLen);
					const s = shaders[shaderId];
					if (s) c.shaderSource(s, src);
				},
				glCompileShader(shaderId) {
					const c = getGl();
					const s = shaders[shaderId];
					if (c && s) c.compileShader(s);
				},
				glGetShaderParameter(shaderId, pname) {
					const c = getGl();
					const s = shaders[shaderId];
					if (!c || !s) return 0;
					if (pname === 0x91B1 && !parallelCompileExt) return 1;
					const res = c.getShaderParameter(s, pname);
					return res ? 1 : 0;
				},
				glCreateProgram() {
					const c = getGl();
					if (!c) return 0;
					const p = c.createProgram();
					programs.push(p);
					return programs.length - 1;
				},
				glAttachShader(programId, shaderId) {
					const c = getGl();
					const p = programs[programId];
					const s = shaders[shaderId];
					if (c && p && s) c.attachShader(p, s);
				},
				glLinkProgram(programId) {
					const c = getGl();
					const p = programs[programId];
					if (c && p) c.linkProgram(p);
				},
				glGetProgramParameter(programId, pname) {
					const c = getGl();
					const p = programs[programId];
					if (!c || !p) return 0;
					if (pname === 0x91B1 && !parallelCompileExt) return 1;
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
					return buffers.length - 1;
				},
				glBindBuffer(target, bufferId) {
					const c = getGl();
					const b = buffers[bufferId];
					if (c) c.bindBuffer(target, b || null);
				},
				glBufferData(target, dataPtr, size, usage) {
					const c = getGl();
					const exports = getWasm();
					if (!c || !exports || !exports.memory) return;
					const view = new Uint8Array(exports.memory.buffer, dataPtr >>> 0, size >>> 0);
					c.bufferData(target, view, usage);
				},
				glCreateTexture() {
					const c = getGl();
					if (!c) return 0;
					const t = c.createTexture();
					textures.push(t);
					return textures.length - 1;
				},
				glDeleteTexture(texId) {
					const c = getGl();
					const t = textures[texId];
					if (c && t) c.deleteTexture(t);
					if (texId > 0 && texId < textures.length) textures[texId] = null;
				},
				glBindTexture(target, texId) {
					const c = getGl();
					const t = texId > 0 ? textures[texId] : null;
					if (c) c.bindTexture(target, t || null);
				},
				glActiveTexture(texture) {
					const c = getGl();
					if (c) c.activeTexture(texture);
				},
				glTexParameteri(target, pname, param) {
					const c = getGl();
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
					if (!ptr) {
						c.texImage2D(target, level, internalformat, width, height, border, format, type, null);
						return;
					}
					if (type === 0x1402) {
						const count = width * height;
						const view = new Int16Array(exports.memory.buffer, ptr, count);
						c.texImage2D(target, level, internalformat, width, height, border, format, type, view);
					} else {
						const byteLen = width * height * 4;
						const view = new Uint8Array(exports.memory.buffer, ptr, byteLen);
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
					if (c) c.drawArrays(mode, first, count);
				},
				glDrawElements(mode, count, type, offset) {
					const c = getGl();
					if (c) c.drawElements(mode, count, type, offset >>> 0);
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
					if (c && s) c.deleteShader(s);
					shaders[shaderId] = null;
				},
				glDeleteProgram(programId) {
					const c = getGl();
					const p = programs[programId];
					if (c && p) c.deleteProgram(p);
					programs[programId] = null;
				},
				glDeleteBuffer(bufferId) {
					const c = getGl();
					const b = buffers[bufferId];
					if (c && b) c.deleteBuffer(b);
					buffers[bufferId] = null;
				}
			};
		}
	};
}

if (typeof window !== 'undefined') {
	window.__wasm_capability_registry = window.__wasm_capability_registry || {};
	window.__wasm_capability_registry.webgl = createWebGlBridge;
	if (typeof window.registerWasmCapability === 'function') {
		window.registerWasmCapability('webgl', createWebGlBridge);
	}
}
