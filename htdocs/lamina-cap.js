/*
 * lamina-bridge.js — Browser WebAssembly bridge for liblamina 2D canvas flush.
 * Provides the "lamina" WASM import module.
 *
 * Automatically mounts onto active [data-bud-canvas] elements.
 * All 3D scene, shader, camera, mesh, and billboard logic lives in pure C over <ttypt/webgl.h>.
 */

function getExport(exports, names) {
	if (!exports) return null;
	for (const name of names) {
		if (typeof exports[name] === 'function') return exports[name];
	}
	return null;
}

const VS_QUAD = `attribute vec2 a_pos;
attribute vec2 a_uv;
varying vec2 v_uv;
void main() {
	v_uv = a_uv;
	gl_Position = vec4(a_pos, 0.0, 1.0);
}`;

const FS_QUAD = `precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
void main() {
	gl_FragColor = texture2D(u_tex, v_uv);
}`;

const shaderDefs = new Map();
let nextShaderId = 1;
let activeShaderId = 0;

const imageIdsByUrl = new Map();
const imageCache = new Map();
let nextImageId = 1;

function compileProgram(gl, vsSrc, fsSrc) {
	try {
		const vs = gl.createShader(gl.VERTEX_SHADER);
		gl.shaderSource(vs, vsSrc);
		gl.compileShader(vs);
		if (!gl.getShaderParameter(vs, gl.COMPILE_STATUS)) return null;

		const fs = gl.createShader(gl.FRAGMENT_SHADER);
		gl.shaderSource(fs, fsSrc);
		gl.compileShader(fs);
		if (!gl.getShaderParameter(fs, gl.COMPILE_STATUS)) return null;

		const prog = gl.createProgram();
		gl.attachShader(prog, vs);
		gl.attachShader(prog, fs);
		gl.linkProgram(prog);
		if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) return null;

		const aPos = gl.getAttribLocation(prog, 'a_pos');
		const aUv = gl.getAttribLocation(prog, 'a_uv');
		const uTex = gl.getUniformLocation(prog, 'u_tex');
		const uTime = gl.getUniformLocation(prog, 'u_time');
		const uRes = gl.getUniformLocation(prog, 'u_res');
		const uParams = gl.getUniformLocation(prog, 'u_params');
		return { prog, aPos, aUv, uTex, uTime, uRes, uParams };
	} catch (e) {
		return null;
	}
}

function initGlPipeline(gl) {
	try {
		const basePipe = compileProgram(gl, VS_QUAD, FS_QUAD);
		if (!basePipe) return null;
		gl.useProgram(basePipe.prog);

		const verts = new Float32Array([
			-1.0, -1.0,  0.0,  1.0,
			 1.0, -1.0,  1.0,  1.0,
			-1.0,  1.0,  0.0,  0.0,
			-1.0,  1.0,  0.0,  0.0,
			 1.0, -1.0,  1.0,  1.0,
			 1.0,  1.0,  1.0,  0.0
		]);

		const vbo = gl.createBuffer();
		gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
		gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);

		const tex = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, tex);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

		return { prog: basePipe.prog, aPos: basePipe.aPos, aUv: basePipe.aUv, tex, vbo, uTex: basePipe.uTex, uTime: basePipe.uTime, uRes: basePipe.uRes };
	} catch (e) {
		return null;
	}
}

export function createLaminaBridge(root, getWasm) {
	const targets = new WeakMap();
	const kicked = new WeakSet();

	function allCanvases() {
		const scope = root && root.querySelectorAll ? root : document;
		let list = scope.querySelectorAll('[data-bud-canvas]');
		if (!list.length && scope !== document) list = document.querySelectorAll('[data-bud-canvas]');
		return list;
	}

	function pickCanvas(width, height) {
		const list = allCanvases();
		let first = null, fit = null;
		for (const c of list) {
			if (!first) first = c;
			if (c.width === width && c.height === height) return c;
			if (!fit && c.hasAttribute && c.hasAttribute('data-bud-canvas-fit')) fit = c;
		}
		if (fit && width > 0 && height > 0) {
			fit.width = width;
			fit.height = height;
			return fit;
		}
		return fit || first;
	}

	function targetFor(el) {
		let t = targets.get(el);
		if (t) return t;
		t = { gl: null, glPipe: null, canvas2d: null, shaderPrograms: new Map() };
		try {
			const glOpts = { preserveDrawingBuffer: true, alpha: true, antialias: true, depth: true };
			t.gl = el.getContext('webgl2', glOpts) || el.getContext('webgl', glOpts);
			if (t.gl) {
				t.glPipe = initGlPipeline(t.gl);
			}
		} catch (e) {
			t.gl = null;
			t.glPipe = null;
		}
		if (!t.gl || !t.glPipe) {
			t.gl = null;
			t.glPipe = null;
			try {
				t.canvas2d = el.getContext('2d');
			} catch (e) {
				t.canvas2d = null;
			}
		}
		targets.set(el, t);
		return t;
	}

	function kickRender() {
		const fn = getExport(getWasm ? getWasm() : null, ['wasm_canvas_render']);
		if (fn && typeof requestAnimationFrame === 'function') requestAnimationFrame(() => fn());
	}

	return {
		mount() {
			for (const c of allCanvases()) {
				if (!kicked.has(c)) {
					kicked.add(c);
					kickRender();
				}
			}
		},
		makeLaminaImports() {
			return {
				lam_canvas_flush(buf_ptr, width, height) {
					const canvasEl = pickCanvas(width, height);
					if (!canvasEl) return;
					const t = targetFor(canvasEl);
					if (!kicked.has(canvasEl)) {
						kicked.add(canvasEl);
						kickRender();
					}
					const exports = getWasm();
					if (!exports || !exports.memory) return;
					const uptr = buf_ptr >>> 0;
					const byteLen = width * height * 4;
					if (uptr + byteLen > exports.memory.buffer.byteLength) return;

					const bytes = new Uint8Array(exports.memory.buffer, uptr, byteLen);

					if (t.gl && t.glPipe) {
						const gl = t.gl;
						gl.viewport(0, 0, canvasEl.width || width, canvasEl.height || height);
						gl.disable(gl.DEPTH_TEST);
						gl.disable(gl.BLEND);
						gl.depthMask(true);
						gl.colorMask(true, true, true, true);
						gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, null);

						let pipe = t.glPipe;
						let def = null;
						if (activeShaderId > 0 && shaderDefs.has(activeShaderId)) {
							def = shaderDefs.get(activeShaderId);
							if (!t.shaderPrograms.has(activeShaderId)) {
								const cp = compileProgram(gl, VS_QUAD, def.fragSrc);
								t.shaderPrograms.set(activeShaderId, cp);
							}
							const cp = t.shaderPrograms.get(activeShaderId);
							if (cp) pipe = cp;
						}

						gl.useProgram(pipe.prog);
						if (pipe.uTime) gl.uniform1f(pipe.uTime, performance.now() * 0.001);
						if (pipe.uRes) gl.uniform2f(pipe.uRes, width, height);
						if (pipe.uParams && def && def.uniforms) gl.uniform4fv(pipe.uParams, def.uniforms);

						gl.bindBuffer(gl.ARRAY_BUFFER, t.glPipe.vbo);
						if (pipe.aPos >= 0) {
							gl.enableVertexAttribArray(pipe.aPos);
							gl.vertexAttribPointer(pipe.aPos, 2, gl.FLOAT, false, 16, 0);
						}
						if (pipe.aUv >= 0) {
							gl.enableVertexAttribArray(pipe.aUv);
							gl.vertexAttribPointer(pipe.aUv, 2, gl.FLOAT, false, 16, 8);
						}

						gl.bindTexture(gl.TEXTURE_2D, t.glPipe.tex);
						gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, bytes);
						gl.drawArrays(gl.TRIANGLES, 0, 6);
						return;
					}

					if (t.canvas2d) {
						const clamped = new Uint8ClampedArray(exports.memory.buffer, uptr, byteLen);
						const imgData = new ImageData(clamped, width, height);
						t.canvas2d.putImageData(imgData, 0, 0);
					}
				},
				lam_shader_register(name_ptr, frag_ptr) {
					const exports = getWasm();
					if (!exports || !exports.memory) return 0;
					const mem = new Uint8Array(exports.memory.buffer);
					let p = name_ptr >>> 0, name = "";
					while (p < mem.length && mem[p] !== 0) name += String.fromCharCode(mem[p++]);
					p = frag_ptr >>> 0;
					let fragSrc = "";
					while (p < mem.length && mem[p] !== 0) fragSrc += String.fromCharCode(mem[p++]);

					const id = nextShaderId++;
					shaderDefs.set(id, { name, fragSrc, uniforms: new Float32Array(16) });
					return id;
				},
				lam_shader_bind(shader_id) {
					activeShaderId = shader_id >>> 0;
				},
				lam_shader_uniforms(shader_id, vals_ptr, count) {
					const exports = getWasm();
					if (!exports || !exports.memory) return;
					const id = shader_id >>> 0;
					const def = shaderDefs.get(id);
					if (!def) return;
					const n = Math.min(count >>> 0, 16);
					const f32 = new Float32Array(exports.memory.buffer, vals_ptr >>> 0, n);
					def.uniforms.set(f32);
				},
				lam_cam_set_3d() {},
				lam_cam_set_2d() {},
				lam_terrain_mesh_flush() {},
				lam_billboard_batch_flush() {},
				lam_fill() {},
				lam_tile_draw() {},
				lam_flush() {},
				lam_image_request(url_ptr) {
					const exports = getWasm();
					if (!exports || !exports.memory) return 0;
					const mem = new Uint8Array(exports.memory.buffer);
					let p = url_ptr >>> 0, url = "";
					while (p < mem.length && mem[p] !== 0) url += String.fromCharCode(mem[p++]);
					if (!url) return 0;
					if (imageIdsByUrl.has(url)) return imageIdsByUrl.get(url);

					const id = nextImageId++;
					imageIdsByUrl.set(url, id);
					const rec = { status: 0, w: 0, h: 0, data: null };
					imageCache.set(id, rec);

					if (typeof Image !== 'undefined') {
						const img = new Image();
						img.decoding = 'async';
						img.onload = () => {
							try {
								const w = img.naturalWidth || img.width || 0;
								const h = img.naturalHeight || img.height || 0;
								if (w > 0 && h > 0) {
									let off = null;
									if (typeof OffscreenCanvas !== 'undefined') {
										off = new OffscreenCanvas(w, h);
									} else if (typeof document !== 'undefined' && document.createElement) {
										off = document.createElement('canvas');
										off.width = w;
										off.height = h;
									}
									if (off) {
										const ctx = off.getContext('2d');
										if (ctx) {
											ctx.clearRect(0, 0, w, h);
											ctx.drawImage(img, 0, 0);
											const idata = ctx.getImageData(0, 0, w, h);
											rec.w = w;
											rec.h = h;
											rec.data = new Uint8Array(idata.data.buffer.slice(0));
											rec.status = 1;
											kickRender();
											return;
										}
									}
								}
								rec.status = 2;
							} catch (e) {
								rec.status = 2;
							}
						};
						img.onerror = () => { rec.status = 2; };
						img.src = url;
					}
					return id;
				},
				lam_image_read(img_id, dst_ptr, max_bytes, out_w_ptr, out_h_ptr) {
					const exports = getWasm();
					if (!exports || !exports.memory) return 0;
					const rec = imageCache.get(img_id >>> 0);
					if (!rec || rec.status !== 1 || !rec.data) return 0;
					const buf = exports.memory.buffer;
					const dv = new DataView(buf);
					if (out_w_ptr) dv.setUint32(out_w_ptr >>> 0, rec.w >>> 0, true);
					if (out_h_ptr) dv.setUint32(out_h_ptr >>> 0, rec.h >>> 0, true);
					const need = rec.data.byteLength;
					const uptr = dst_ptr >>> 0;
					if (uptr && (max_bytes >>> 0) >= need && uptr + need <= buf.byteLength) {
						new Uint8Array(buf, uptr, need).set(rec.data);
						return 1;
					}
					return 0;
				}
			};
		},
		destroy() {}
	};
}

if (typeof window !== 'undefined') {
	window.__wasm_capability_registry = window.__wasm_capability_registry || {};
	window.__wasm_capability_registry.lamina = (root, getWasm) => {
		const b = createLaminaBridge(root, getWasm);
		return {
			mount() { b.mount(); },
			destroy() { b.destroy(); },
			makeImports() { return b.makeLaminaImports(); }
		};
	};
}
