import { createLaminaRenderer } from "./renderer.js";

function getExport(exports, names) {
  if (!exports) return null;
  for (const name of names) {
    if (typeof exports[name] === "function") return exports[name];
  }
  return null;
}

export function createLaminaBridge(root, getWasm) {
  const renderers = new WeakMap();
  const kicked = new WeakSet();

  function allCanvases() {
    if (!root) {
      if (typeof document !== "undefined") {
        return Array.from(document.querySelectorAll("[data-bud-canvas]"));
      }
      return [];
    }
    const list = root.querySelectorAll("[data-bud-canvas]");
    if (list && list.length) return Array.from(list);
    return root.tagName === "CANVAS" ? [root] : [];
  }

  function pickCanvas(width, height) {
    const list = allCanvases();
    let first = null, fit = null;
    for (const c of list) {
      if (!first) first = c;
      if (c.width === width && c.height === height) return c;
      if (!fit && c.hasAttribute && c.hasAttribute("data-bud-canvas-fit")) fit = c;
    }
    if (fit) {
      fit.width = width;
      fit.height = height;
      return fit;
    }
    return first;
  }

  function getRenderer(canvasEl) {
    let r = renderers.get(canvasEl);
    if (!r) {
      r = createLaminaRenderer(canvasEl);
      renderers.set(canvasEl, r);
    }
    return r;
  }

  function kickRender() {
    const fn = getExport(getWasm(), ["wasm_canvas_render"]);
    if (fn && typeof requestAnimationFrame === "function") {
      requestAnimationFrame(() => fn());
    }
  }

  return {
    mount() {
      for (const c of allCanvases()) {
        getRenderer(c);
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
          const renderer = getRenderer(canvasEl);
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
          renderer.render(bytes, width, height);
        },
        lam_fill() {},
        lam_tile_draw() {},
        lam_flush() {}
      };
    },

    destroy() {
      // WeakMap keys are garbage-collected automatically
    }
  };
}

if (typeof window !== "undefined") {
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
