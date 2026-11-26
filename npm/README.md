<p align="center">
  <img src="https://raw.githubusercontent.com/tty-pt/liblamina/main/assets/logo.png" alt="@tty-pt/lamina logo" width="220" />
</p>

# @tty-pt/lamina

[![npm version](https://img.shields.io/npm/v/@tty-pt/lamina.svg)](https://www.npmjs.com/package/@tty-pt/lamina)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](https://github.com/tty-pt/liblamina/blob/main/LICENSE)
[![TypeScript Ready](https://img.shields.io/badge/TypeScript-Ready-3178C6?logo=typescript)](#)
[![Node.js Native](https://img.shields.io/badge/Node.js-N--API-339933?logo=node.js)](#)
[![WebAssembly](https://img.shields.io/badge/WebAssembly-WebGL2%20%2B%20Canvas-654FF0?logo=webassembly)](#)
[![Zero Dependencies](https://img.shields.io/badge/dependencies-0-success)](#)

> Universal Node.js and Browser bindings for **`liblamina`**, the immediate-mode 2D/3D graphics layer, tilemap renderer, perspective camera math, flexbox UI layout engine, and WebAssembly-to-WebGL2 hardware capability bridge. Canonical C headers: `<ttypt/lam.h>`, `<ttypt/lam-ui.h>`, `<ttypt/lam-cam.h>`, `<ttypt/lam-tm.h>`, and `<ttypt/webgl.h>`.

📖 **Full C & Graphics Documentation**: See the main [**liblamina GitHub README**](https://github.com/tty-pt/liblamina#readme) for desktop OpenGL/GLFW, Linux `/dev/fb0` framebuffer setup, 3D terrain/water/billboard rendering, and C API details.

---

## 1. Overview & Domain Concepts

`liblamina` (`@tty-pt/lamina`) provides an ultra-compact immediate-mode rendering and layout pipeline across native C, Node.js, and Browser WebGL2/Canvas runtimes:

1. **Immediate-Mode Pixel Surfaces (`LaminaSurface`)**:
   - Direct RGBA pixel buffer manipulation (`clear`, `fill`, `rect`, `line`, `paint`, `pick`) backed by WebAssembly linear memory with one-call presentation to HTML5 `<canvas>` or WebGL2 textures (`attachCanvas`, `createLaminaRenderer`).
2. **2D & 3D Viewport & Camera Projection (`ViewportController`)**:
   - Projects world coordinates to screen pixels (`worldToScreen`) and unprojects screen pointers back into world space (`screenToWorld`) using compiled C/WASM camera matrices.
3. **CSS Flexbox & Grid Layout Engine (`FlexNode`)**:
   - Computes hierarchical Flexbox and CSS Grid bounds (`setSize`, `setPadding`, `setFlex`, `computeLayout`, `getBounds`) without requiring a browser DOM (C builds also include a pure-C CSS parser, TrueType fonts, built-in JPEG decoding, and `<ttypt/lam-bud.h>`).
4. **WebAssembly-to-WebGL2 Capability Bridge (`createLaminaBridge`, `htdocs/webgl-cap.js`)**:
   - Wires WebGL2 host imports, VAOs (`glCreateVertexArray`, `glBindVertexArray`, `LAM_GL_CMD_BIND_VAO`), UBOs (`glBindBufferBase`), flat `uint32_t` command ring submission (`glCmdFlush`), zero-alloc `glBufferSubData`, offscreen half-float FBOs (`GL_R16F`, `GL_RGBA16F`), 3D / 2D-array textures (`glTexStorage3D`, `glTexImage3D`, `glTexSubImage3D`, `glGenerateMipmap`), GPU hardware instancing (`glDrawArraysInstanced`, `glDrawElementsInstanced`, `glVertexAttribDivisor`), pass timers (`EXT_disjoint_timer_query_webgl2`), and parallel shader compilation (`KHR_parallel_shader_compile`) into `@tty-pt/stele` WASM modules.

---

## 2. Installation

```bash
npm install @tty-pt/lamina
```

**Native OS Packages (`tty.pt`)**: Prebuilt native C packages (`liblamina`) for Linux (`apt`, `apk`, `pacman`, `dnf`), macOS (`brew`), Windows (`winget`, MSYS2), and OpenBSD (`pkg_add`) are available via the [**tty.pt package repositories**](https://github.com/tty-pt/ci/blob/main/docs/install.md).

---

## 3. Quickstart

```typescript
import {
  LaminaSurface,
  ViewportController,
  FlexNode,
  rgba,
  createLaminaBridge
} from "@tty-pt/lamina";

// 1. Immediate-mode pixel surface in WASM linear memory
const surface = new LaminaSurface(320, 240);
surface
  .clear(rgba(32, 32, 40, 255))
  .fill(20, 20, 100, 60, rgba(80, 180, 255, 255))
  .line(0, 0, 319, 239, rgba(255, 255, 255, 255), 2);

const pixels = surface.getPixels(); // Uint32Array view
console.log("Top-left pixel:", pixels[0].toString(16));
surface.free();

// 2. Camera coordinate projection & unprojection
const viewport = new ViewportController(1280, 720);
viewport.setCamera(100, 200, 1.5 /* zoom */);
const screenPos = viewport.worldToScreen(100, 200);
console.log("Screen center:", screenPos); // { x: 640, y: 360 }

// 3. Headless Flexbox UI layout calculation
const root = new FlexNode("row").setPadding(8, 8, 8, 8);
const sidebar = new FlexNode("column").setSize(200, 0).setFlex(0, 0);
const content = new FlexNode("column").setFlex(1, 1);
root.addChild(sidebar).addChild(content).computeLayout(1024, 768);
console.log("Content bounds:", content.getBounds());
root.free();
```

---

## 4. JavaScript / TypeScript API Reference

### `LaminaSurface`
- `constructor(width: number, height: number, options?: { canvas?: HTMLCanvasElement })`
- `.clear(color?: number): this`
- `.fill(x: number, y: number, w: number, h: number, color: number): this`
- `.rect(x: number, y: number, w: number, h: number, color: number, thickness?: number): this`
- `.line(x0: number, y0: number, x1: number, y1: number, color: number, thickness?: number): this`
- `.paint(x: number, y: number, color: number): this` / `.pick(x: number, y: number): number`
- `.getPixels(): Uint32Array` / `.getClampedPixels(): Uint8ClampedArray`
- `.flush(targetCanvas?: HTMLCanvasElement): this` / `.free(): void`

### `ViewportController`
- `constructor(width?: number, height?: number)`
- `.setCamera(x: number, y: number, zoom?: number): void`
- `.worldToScreen(worldX: number, worldY: number): { x: number; y: number }`
- `.screenToWorld(screenX: number, screenY: number): { x: number; y: number }`

### `FlexNode`
- `constructor(direction?: "row" | "column")`
- `.setSize(w: number, h: number): this` / `.setPadding(l: number, t: number, r: number, b: number): this` / `.setFlex(grow?: number, shrink?: number): this`
- `.addChild(child: FlexNode): this`
- `.computeLayout(availableWidth: number, availableHeight: number, startX?: number, startY?: number): this`
- `.getBounds(): { x: number; y: number; width: number; height: number }` / `.free(): void`

### Color, Canvas & WebGL2 Capability Bridge
- `rgba(r, g, b, a?)`, `rgb(r, g, b)`, `colorR(c)`, `colorG(c)`, `colorB(c)`, `colorA(c)`: Packed 32-bit color helpers.
- `createLaminaRenderer(canvasEl)` / `attachCanvas(canvasEl)`: WebGL2 / 2D Canvas surface presenter.
- `createLaminaBridge(root?, getWasm?)`: Registers the WebGL2 & Lamina host capability imports (`webgl_*`, `lam_*`) on a Stele runtime.
- `getLaminaWasm(): WebAssembly.Instance` / `version(): string`.

---

## 5. C Development Headers & Native Integration

```makefile
CFLAGS += -I./node_modules/@tty-pt/lamina/include
LDLIBS += -llamina
```

---

## License

BSD-2-Clause — Copyright (c) 2026, Paulo Quirino <quirinpa@gmail.com>. See [LICENSE](https://github.com/tty-pt/liblamina/blob/main/LICENSE).
