## [Unreleased] (target: v1.2.0)

### Added
- **Native OpenGL/GLFW WebGL Backend, Pure-C CSS Engine, Built-in JPEG Decoder & TrueType Fonts (`src/webgl_native.c`, `src/ui-css.c`, `src/jpeg.c`, `src/font.c`, `include/ttypt/lam-font.h`, `include/ttypt/webgl.h`)**:
  - Added `src/webgl_native.c` implementing the `<ttypt/webgl.h>` (`lam_gl*`) surface directly on native desktop OpenGL/GLFW, enabling identical 3D/WebGL2 rendering code across native binaries and browser WASM (`tests/test_webgl.c`).
  - Added a pure-C CSS stylesheet parser (`src/ui-css.c`, `tests/test_css_parser.c`) and expanded the layout/style engine (`src/ui.c`, `src/ui-style.c`, `src/shadow.c`) with CSS Grid tracks, `calc(* /)` expressions, inset rings, SVG properties, and Porter-Duff FBO alpha compositing.
  - Added a built-in pure-C JPEG image decoder (`src/jpeg.c`) integrated into `src/img.c`.
  - Added a built-in TrueType (TTF) font engine (`src/font.c`, `include/ttypt/lam-font.h`: `lam_font_open_ttf()`, `lam_font_add_fallback()`, `lam_font_is_ttf()`, `lam_font_line_height()`, `lam_font_ascent()`, `lam_font_draw_px()`, `lam_font_measure_px()`, `tests/test_fonts_ttf.c`).
- **`lam_ui` Tree Mutation Ops & Zero-Coupling Bud Host Adapter (`include/ttypt/lam-bud.h`, `include/ttypt/lam-ui.h`, `src/ui.c`)**:
  - Added DOM-style tree mutation operations to `lam_ui` and a header-only `lam-bud.h` adapter (`lam_bud_host_init()`, `lam_bud_host_ops()`, `lam_bud_sync_layout()`, `lam_bud_dispatch_event()`, `lam_bud_dispatch_click()`) bridging `libbud` / `.cx` UI trees onto `liblamina` (`tests/music_panel.cx`, `tests/test_bud_lamina.c`, `tests/test_cx_lamina.c`).
- **WebGL2 VAOs, UBOs, Flat Command Ring (`glCmdFlush`), Pass Timers & Half-Float Textures (`include/ttypt/webgl.h`, `src/webgl_native.c`, `htdocs/webgl-cap.js`, `wasm-allowed-imports.lst`)**:
  - Added WebGL2 VAO bindings (`glCreateVertexArray`, `glDeleteVertexArray`, `glBindVertexArray`, `LAM_GL_CMD_BIND_VAO`), `glTexStorage3D`, `glGenerateMipmap`, `GL_R16F`, `GL_RGBA16F`, `GL_HALF_FLOAT`, and half-float color buffer extension enablement.
  - Added WebGL2 UBO bindings (`GL_UNIFORM_BUFFER`, `glBindBufferBase`, `glGetUniformBlockIndex`, `glUniformBlockBinding`) and a flat `uint32_t` command ring buffer (`glCmdFlush()`).
  - Added zero-alloc `glBufferSubData()`, `glTexSubImage2D()` (including `GL_FLOAT`), `GL_TEXTURE_2D_ARRAY` bridge support, and `EXT_disjoint_timer_query_webgl2` pass timers (`glTimerBegin()`, `glTimerEnd()`).
- **GPU Hardware Instancing & 3D / 2D-Array Texture Bindings (`include/ttypt/webgl.h`, `npm/include/ttypt/webgl.h`, `htdocs/webgl-cap.js`, `wasm-allowed-imports.lst`)**:
  - Added `glDrawArraysInstanced`, `glDrawElementsInstanced`, and `glVertexAttribDivisor` host imports and WebGL2 bridge dispatchers for single-draw-call instanced billboard and terrain batching.
  - Added 3D texture / 2D texture array upload bindings (`glTexImage3D`, `glTexSubImage3D`, `GL_TEXTURE_2D_ARRAY`) and asynchronous shader compilation status polling (`KHR_parallel_shader_compile`).

### Changed
- **Browser Capability Bridge Consolidation (`htdocs/webgl-cap.js`, `htdocs/lamina-cap.js`)**:
  - Removed legacy `htdocs/lamina-bridge.js` and `htdocs/webgl-bridge.js`, consolidating all WebGL2 and 2D Lamina browser capability hooks into `htdocs/webgl-cap.js` (`createWebGlBridge`) and `htdocs/lamina-cap.js` (`createLaminaBridge`).
  - Optimized `htdocs/webgl-cap.js` with fast-path `getGl()`, cached WASM memory views, gated `preserveDrawingBuffer`, and optional performance counters.

### Fixed
- **WebGL2 State Replay, Native Re-Entrancy & FBO Guards (`htdocs/webgl-cap.js`, `htdocs/lamina-cap.js`, `src/webgl_native.c`, `src/liblamina.c`)**:
  - Tracked per-unit texture bindings and `GL_TEXTURE_3D` bindings across context swaps, replayed link-time scalar uniforms and `STATIC_DRAW` subdata on swap, cleared null FBO textures, excluded plain canvases in `lamina-cap.js`, and skipped worker `fallbackOffscreen`.
  - Fixed native re-entrant begin/end framing, default-FBO viewport auto-fit, and `lam_clear()` 2D GL state restoration (`src/webgl_native.c`, `src/liblamina.c`, `tests/test_webgl.c`).
  - Guarded `pixelStorei(UNPACK_ALIGNMENT, 1)` on `texImage3D` / `texSubImage3D` and guarded offscreen FBO `glViewport` against resizing the main canvas swapchain.

## [1.1.1] - 2026-11-16

### Added
- **Offscreen Framebuffer (FBO), Renderbuffer & Stencil Imports (`include/ttypt/webgl.h`, `htdocs/webgl-cap.js`, `htdocs/lamina-worker.js`)**:
  - Added offscreen framebuffer (`webgl_create_framebuffer`, `webgl_bind_framebuffer`, `webgl_framebuffer_texture2d`), renderbuffer (`webgl_create_renderbuffer`, `webgl_bind_renderbuffer`, `webgl_renderbuffer_storage`, `webgl_framebuffer_renderbuffer`), and viewport/scissor/stencil bindings (`webgl_stencil_func`, `webgl_stencil_op`, `webgl_stencil_mask`, `webgl_color_mask`) for low-resolution volumetric cloud FBO rendering and multi-pass compositing.
  - Enabled single-sampled main buffer configuration for crisp pixel/retro pass-through when requested.

### Changed
- **Node Addon & Substrate Alignment (`npm/native/LIBS`, `npm/package.json`)**:
  - Linked the Node-API native addon exclusively against camera math (`src/lam_cam.c`) without requiring host desktop GLFW/GLEW libraries.
  - Upgraded `@tty-pt/stele` `peerDependencies` (`>=1.7.0`) and `devDependencies` (`^1.7.0`).
- **Arch Linux (`pacman`) Packaging (`.github/workflows/ci.yml`)**:
  - Added `pacman` to `publish_to` and configured `deps_pacman: "libstele,libcorm,glfw,libpng,glew"` so `liblamina` publishes across all 7 OS targets (`deb`, `apk`, `rpm`, `pacman`, `brew`, `openbsd`, `winget`).

## [1.1.0] - 2026-11-13

- **WebGL Worker Offloading & Parallel Shader Compilation**:
  - Unified native C 2D/3D engine with pure-C WebGL2 pass-through bridge, camera math, and standardized README.
  - Added KHR_parallel_shader_compile support and offscreen canvas worker dispatch.
  - Perspective camera projection, billboard alignment, and multi-scale water optics.
- **Libstele v1.6.0 Substrate Alignment**:
  - Upgraded substrate baseline to @tty-pt/stele@^1.6.0 (>=1.6.0).

## [1.0.1]

- **Universal Substrate Migration (`libstele`)**:
  - Replaced legacy `libqsys` with `libstele` (`-lstele`) across build configuration, pkg-config definition (`lamina.pc`), and CI packaging workflows.
  - Aligned packaging dependencies across Debian, Alpine, RedHat/Rocky, Arch Linux, macOS, OpenBSD, and Windows.

## [1.0.0]

- **Inaugural Release of liblamina** (formerly `libqgl`):
  - Consolidated cross-platform 2D graphics layer and flexbox UI layout engine in pure C.
  - Unified immediate-mode rendering across desktop displays (OpenGL via GLFW on Linux X11/Wayland, macOS, Windows) and direct hardware on embedded Linux (via memory-mapped `/dev/fb0` without X11 or Wayland).
  - Standalone shared library `lib/liblamina.so` with pkg-config package `lamina.pc`.

- **API Modernization (`lam_*` / `LAM_*`)**:
  - Reorganized public headers under `include/ttypt/`:
    * `<ttypt/lam.h>` — Core rendering, textures, colors, and lifecycle.
    * `<ttypt/lam-ui.h>` — Declarative flexbox UI tree, styling, and layout engine.
    * `<ttypt/lam-font.h>` — Pre-rendered bitmap font loading, measurement, and text wrapping.
    * `<ttypt/lam-tm.h>` — Sprite tilemap subdivision and batch 2D map grid rendering.
    * `<ttypt/lam-key.h>` — Hardware keyboard event codes, gamepad buttons, and directional hats.
  - Canonical `lam_*` symbols across lifecycle (`lam_init`, `lam_size`, `lam_flush`, `lam_poll`), primitives (`lam_fill`, `lam_clear`, `lam_rect`, `lam_line`), and textures (`lam_tex_*`).
  - Typed color packing and unpacking helpers: `LAM_RGBA(r, g, b, a)`, `LAM_RGB(r, g, b)`, channel extractors (`LAM_COLOR_R/G/B/A`), and standard palette constants (`LAM_COLOR_WHITE`, `LAM_COLOR_BLACK`, `LAM_COLOR_RED`, `LAM_COLOR_GREEN`, `LAM_COLOR_BLUE`, `LAM_COLOR_YELLOW`, `LAM_COLOR_CYAN`, `LAM_COLOR_MAGENTA`, `LAM_COLOR_TRANSPARENT`).

- **Flexbox UI Engine (`lam_ui_*`)**:
  - Declarative container constructors: `lam_ui_flex(parent, dir)`, `lam_ui_box(parent, w, h, bg_color)`, `lam_ui_label(parent, text, font_ref, color)`.
  - Direct style setters: `lam_ui_set_size`, `lam_ui_set_bg`, `lam_ui_set_padding`, `lam_ui_set_flex`, `lam_ui_set_align`.
  - Full CSS flexbox semantics: `LAM_UI_ROW`, `LAM_UI_COLUMN`, `LAM_UI_JUSTIFY_*`, `LAM_UI_ALIGN_*`, padding, borders, drop shadows (`lam_box_shadow`), and rounded corners (`lam_border_radius`).
  - High-performance layout cache powered by [libcorm](https://github.com/tty-pt/libcorm) for sub-millisecond layout reuse.

- **Sprite Tilemaps & Font Atlases**:
  - Pre-rendered bitmap font atlases (`lam_font_open`, `lam_font_draw`, `lam_font_measure`) with word wrapping and vertical overflow inspection.
  - Subdivided sprite atlases (`lam_tm_new`, `lam_tm_get`, `lam_tile_draw`) and batch grid map blitter (`lam_tile_map_draw`).

- **Engine & Driver Bug Fixes**:
  - Registered `cm_gl_tex` in `libcorm` (`corm_reg`), resolving corrupted texture descriptor lookups and eliminating vertical line rendering artifacts.
  - Corrected presentation shader UV orientation (`VS_PRESENT`) to ensure right-side up display.
  - Unified texture uploading on `GL_RGBA` with `GL_CLAMP_TO_EDGE` wrap modes.
  - Added Core Profile GLEW initialization fallback for headless environments.
