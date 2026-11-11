<p align="center">
  <img src="assets/logo.png" alt="liblamina logo" width="220" />
</p>

# liblamina

[![npm](https://img.shields.io/badge/npm-@tty--pt/lamina-CB3837?logo=npm)](npm/README.md)
[![C99](https://img.shields.io/badge/C-C99-555?logo=c)](#)
[![BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![2D & 3D Graphics](https://img.shields.io/badge/engine-2D%20%26%203D%20graphics-4B8BBE)](#)
[![OpenGL, WebGL2 & Framebuffer](https://img.shields.io/badge/backends-OpenGL%20%7C%20WebGL2%20%7C%20GLFW%20%7C%20Linux%20Framebuffer-16A34A)](#)
[![Flexbox UI](https://img.shields.io/badge/layout-flexbox%20UI-blueviolet)](#)

> Ultra-portable 2D/3D immediate-mode rendering, bitmap fonts, tilemaps, perspective camera math, and flexbox-style UI layout engine in pure C with OpenGL, GLFW, WebGL2/WASM, and direct Linux framebuffer backends. Canonical header is `<ttypt/lam.h>` and pkg-config package `lamina.pc`.

`liblamina` provides an immediate-mode 2D/3D graphics layer and flexbox-style UI layout engine engineered for small engines, tools, kiosks, and 2D/2.5D games. It provides a unified immediate-mode API across desktop environments (OpenGL via GLFW on Linux X11/Wayland, macOS, Windows), browser environments (WebGL2 & HTML5 Canvas via WebAssembly), and direct hardware on embedded Linux (via memory-mapped `/dev/fb0` without X11 or Wayland).

---

## 1. Quickstart

Initialize the display, obtain window dimensions, fill a rectangle, and present the frame:

```c
#include <ttypt/lam.h>

int main(void)
{
	uint32_t w, h;

	lam_init();
	lam_size(&w, &h);
	lam_fill(0, 0, w, h, 0xFF202020); /* Dark gray background */
	lam_flush();
	return 0;
}
```

### Compiling and Linking

`liblamina` links via `-llamina` using the `lamina` pkg-config definition:

```bash
cc my_app.c $(pkg-config --cflags --libs lamina)
```

Or manually:

```bash
cc my_app.c -I/usr/local/include -L/usr/local/lib -llamina -lcorm -lstele -lpng -lxxhash
```

---

## 2. Why liblamina? (2D & 2.5D Graphics & UI Without the Bloat)

Most graphics libraries either drag in massive runtime dependencies (SDL2, Raylib, heavyweight windowing systems) or lock developers into high-level GUI toolkits (GTK, Qt, Electron) that are impossible to run on headless servers, embedded Linux, browser WebAssembly reactors, or bare framebuffer kiosks (`/dev/fb0`).

`liblamina` provides an alternative path in pure C:
- **Zero Heavy Frameworks**: Compact immediate-mode rendering pipeline with procedural pixel hooks (`lam_render`), alpha blending, and double-buffered frame presentation (`lam_flush`).
- **Flexbox UI Built In**: True CSS-style flexbox container trees (`lam_ui_div`), direction/justify/align layout calculations, computed sizing, and caching (`lam-ui.h`) powered by [libcorm](https://github.com/tty-pt/libcorm).
- **2.5D / 3D Camera & Depth Pipeline**: Perspective and orthographic $4 \times 4$ matrix math, ground-plane ray unprojecting, 3D tilted-camera tile drawing, and GPU billboard batching (`lam-cam.h`, `lam-tm.h`).
- **Run on Raw Hardware or Browser WebGL2**: Seamlessly falls back to `/dev/fb0` when no display server is present, auto-detecting screen geometry and bit depth via `ioctl(fb_fd, FBIOGET_VSCREENINFO, ...)`, or streams directly to WebGL2 in the browser via `htdocs/lamina-bridge.js`.
- **Ecosystem Integration**: Direct integration with [libcorm](https://github.com/tty-pt/libcorm) for UI state caching and [libstele](https://github.com/tty-pt/libstele) for cross-platform system primitives and isomorphic WASM boundaries.

### The Five Jobs It Does
1. **The 2D Immediate-Mode Canvas**: Primitives, rectangle fills, pixel-by-pixel procedural shaders (`lam_render`), color tinting, and frame presentation via `lam_flush`.
2. **Textures, Sprites & Tilemaps**: Fast PNG loading, pixel picking/painting, spritesheet subdivision (`lam_tm_new`), and batch 2D/3D tilemap drawing (`lam_tile_map_draw`, `lam_tile_draw_ground_3d`, `lam_tile_draw_billboard_3d`).
3. **3D Camera & WebGL2 Streaming**: $4 \times 4$ column-major matrix operations, tilted-camera setup (`lam_cam_set_3d`), screen-to-ground ray casting (`lam_unproject_ground`), and WebGL2 terrain/water/billboard hardware flushing (`lam_terrain_mesh_flush`, `lam_billboard_batch_flush`).
4. **Bitmap Font Rendering**: Pre-rendered bitmap font atlases (`lam-font.h`) with integer scaling, word wrapping, and bounding-box measurement.
5. **Flexbox UI & Input Dispatch**: Declarative UI trees (`lam-ui.h`) with stylesheet rules, box shadows, border radii, and non-blocking keyboard/gamepad callbacks (`lam-key.h`).

---

## 3. API Overview & Headers

The API contract lives across seven modular headers in `<ttypt/>`:

### 1. Core Rendering & Primitives (`<ttypt/lam.h>`)
- **Lifecycle & Frame Presentation**: `lam_init()`, `lam_deinit()`, `lam_size(&w, &h)`, `lam_flush()`, `lam_poll()`.
- **Screen Clearing & Primitives**: `lam_clear(color)`, `lam_fill(x, y, w, h, color)`, `lam_rect(x, y, w, h, border_color, border_width)`, `lam_line(x0, y0, x1, y1, color, thickness)`.
- **Color Helpers**: `LAM_RGBA(r, g, b, a)`, `LAM_RGB(r, g, b)`, `LAM_COLOR_R/G/B/A(c)`, and standard palette constants (`LAM_COLOR_WHITE`, `LAM_COLOR_BLACK`, `LAM_COLOR_RED`, etc.).
- **Procedural Shading**: `lam_render(lambda, x, y, w, h, ctx)` for custom pixel-by-pixel generators.
- **Texture Operations**: `lam_tex_load(filename)`, `lam_tex_draw(tex, x, y, dw, dh)`, `lam_tex_draw_x(...)`, `lam_tex_pick(tex, x, y)`, `lam_tex_paint(tex, x, y, color)`, `lam_tex_save(tex)`, `lam_tint(color)`.
- **WebGL2 / WASM Streaming Hooks**: `lam_canvas_flush()`, `lam_shader_register()`, `lam_shader_bind()`, `lam_shader_uniforms()`, `lam_terrain_mesh_flush()`, `lam_billboard_batch_flush()`.

### 2. Flexbox UI Layout & Declarative Builders (`<ttypt/lam-ui.h>`)
- **Declarative Element Constructors**:
  - `lam_ui_flex(parent, dir)`: Create a flex container with `display: flex` in row or column orientation.
  - `lam_ui_box(parent, w, h, bg_color)`: Create a sized container with background color.
  - `lam_ui_label(parent, text, font_ref, color)`: Create a text label node with font and color.
  - `lam_ui_new(parent, style)`: Low-level element creation.
- **Direct Style Setters**: `lam_ui_set_size(div, w, h)`, `lam_ui_set_bg(div, color)`, `lam_ui_set_padding(div, left, top, right, bottom)`, `lam_ui_set_flex(div, grow, shrink, basis)`, `lam_ui_set_align(div, align_items, justify_content)`.
- **Layout Accessors**: `lam_ui_get_x()`, `lam_ui_get_y()`, `lam_ui_get_width()`, `lam_ui_get_height()`, `lam_ui_get_text()`, `lam_ui_get_class()`.
- **Styling**: `lam_ui_stylesheet_init()` and `lam_ui_stylesheet_add()` to configure background colors, borders, margins, padding, and drop shadows (`lam_box_shadow`, `lam_border_radius`).
- **Caching Engine**: Render cache powered by [libcorm](https://github.com/tty-pt/libcorm) for high-performance layout reuse.

### 3. Pre-Rendered Bitmap Fonts (`<ttypt/lam-font.h>`)
- **Font Atlas Loading**: `lam_font_open(path)` loads pre-rendered bitmap font atlases (e.g. converted from BDF files via `bdf2tm.py`).
- **Text Rendering**: `lam_font_draw(...)` renders text at arbitrary integer scales with automatic word wrapping and custom color tinting.
- **Measurement**: `lam_font_measure(...)` calculates bounding boxes before drawing.

### 4. Sprite Tilemaps & 3D Sprites (`<ttypt/lam-tm.h>`)
- **Atlas Subdivision**: `lam_tm_new(tex, tile_w, tile_h)` segments a spritesheet into a 2D tile grid.
- **Tile Presentation**: Draw individual tiles by index (`lam_tile_draw(...)`).
- **Batch Grid Map Drawing**: `lam_tile_map_draw(tm_ref, tiles, map_w, map_h, x, y, scale)` renders entire 2D map grids from a tile index buffer in a single call.
- **3D Ground & Billboard Tiles**: `lam_tile_draw_ground_3d(tm_ref, idx, wx, wy, wz, tw, th)` and `lam_tile_draw_billboard_3d(tm_ref, idx, wx, wy, wz, sw, sh, anchor_y)`.

### 5. 3D Camera & Matrix Math (`<ttypt/lam-cam.h>`)
- **Matrix Math (`lam_mat4_t`)**: `lam_mat4_identity`, `lam_mat4_mul`, `lam_mat4_translate`, `lam_mat4_scale`, `lam_mat4_rotate_x/y/z`, `lam_mat4_perspective`, `lam_mat4_ortho`, `lam_mat4_look_at`, `lam_mat4_invert`.
- **Camera Control**: `lam_cam_set_3d(...)`, `lam_cam_set_2d()`, `lam_cam_is_3d()`, `lam_cam_get()`, `lam_cam_get_vp()`, `lam_unproject_ground()`.

### 6. Input Mapping & Gamepads (`<ttypt/lam-key.h>`)
- **Callbacks**: `lam_key_reg(key, callback)` and `lam_key_default_reg(callback)` for non-blocking raw key events.
- **Key Code Translation**: `lam_key_val(code)` and `lam_key_parse(target, len, code, flags)` map hardware keycodes to ASCII/UTF-8 characters.
- **Gamepads & Hats**: Constants for directional pads and buttons (`LAM_HAT_*`, `LAM_GAMEPAD_*`).

### 7. WebGL2 Hardware Bridge (`<ttypt/webgl.h>` & `htdocs/lamina-bridge.js`)
- Low-level WebAssembly-to-WebGL2 binding definitions and the self-contained browser runtime (`createLaminaBridge`) supporting continuous 3D terrain meshes, multi-scale procedural water optics, and hardware-instanced billboards.

---

## 4. Display Backends

- **Linux Desktop**: Automatically uses GLFW when running under an active X11 or Wayland session (`DISPLAY` or `WAYLAND_DISPLAY` set).
- **Embedded Linux Framebuffer**: Falls back to direct `/dev/fb0` rendering when no display server is present, accessing screen buffers without X11.
- **macOS & Windows**: Native GLFW backend with OpenGL acceleration.
- **Browser (WebGL2 / Canvas)**: Hardware-accelerated WebGL2 and 2D Canvas presentation via `htdocs/lamina-bridge.js` and `@tty-pt/lamina`.

### Linux Framebuffer Setup (`/dev/fb0`)

To run directly on the Linux framebuffer without an X11 or Wayland display server:

1. **Device Permissions**: Ensure your user has read/write permissions to the framebuffer and input event devices:
   ```bash
   sudo usermod -aG video,input $USER
   ```
2. **TTY Console**: Switch to a raw virtual console (e.g. `Ctrl+Alt+F3`) or run in an embedded environment (e.g. Raspberry Pi or kiosk).
3. **Auto-Detection**: `liblamina` automatically reads screen resolution, row stride, and bit depth via `ioctl(fb_fd, FBIOGET_VSCREENINFO, ...)` supporting standard 16bpp and 32bpp RGBA/BGRA modes.

---

## 5. Examples

Six complete example programs demonstrate features from raw pixels to styled UI trees:

```sh
# Generate test fixtures and build examples
python3 tests/generate_fixtures.py
make examples

# Run an example
LD_LIBRARY_PATH=./lib ./examples/01_basic_rendering
```

| Example | Source File | Description |
| :--- | :--- | :--- |
| **01 Basic Rendering** | `examples/01_basic_rendering.c` | Window creation, color fills, and double-buffered presentation |
| **02 Textures** | `examples/02_textures.c` | Loading PNG images, drawing sprites, and sub-region blitting |
| **03 Fonts** | `examples/03_fonts.c` | Bitmap font rendering, scaling, measurement, and text wrapping |
| **04 Tilemaps** | `examples/04_tilemaps.c` | Segmenting sprite atlases into grid tilemaps |
| **05 UI Layout** | `examples/05_ui_layout.c` | Constructing flexbox container trees with `lam_ui_div` |
| **06 Advanced UI** | `examples/06_ui_advanced.c` | Advanced UI styling, borders, layout inspection, and caching |

See [examples/README.md](examples/README.md) for detailed walkthroughs.

---

## 6. Testing & Test Suite

The library includes an extensive test suite covering all public subsystems:

```sh
make test-build       # builds all 9 C test binaries
make test             # executes unit tests across core, textures, fonts, tilemaps, 3D camera, and UI
make npm-test         # builds and executes the Node.js & WebAssembly test suite
```

Test coverage spans:
- Core rendering and viewport clipping
- PNG texture loading and sub-region blitting
- Font atlas parsing and wrapping math
- Tilemap indexing and bounds checking
- 3D camera matrix math, perspective projection, and ground-plane ray unprojecting (`test_cam_3d`)
- Flexbox layout hierarchy calculations and sizing algorithms
- UI styling and `libcorm` layout cache eviction
- WebGL2 & WebAssembly surface/bridge integration (`npm/tests/lamina.test.mjs`)

---

## 7. Installation & Build

### Prebuilt Packages
Prebuilt packages for Linux (APT / Alpine / Arch / Fedora-RHEL), macOS (Homebrew), Windows (winget / MSYS2), and OpenBSD are distributed via [tty.pt packages](https://github.com/tty-pt/ci/blob/main/docs/install.md). Use **liblamina** as the package name.

### Dependencies

- **Core**: `libcorm`, `libstele`, `libpng`, `libxxhash`
- **Desktop (GLFW backend)**: `libglfw3-dev`, `libglew-dev`, OpenGL
- **Embedded (Framebuffer backend)**: Linux `/dev/fb0` (standard in Linux kernel)

On Debian / Ubuntu:
```bash
sudo apt-get install build-essential pkg-config libglfw3-dev libglew-dev libpng-dev libxxhash-dev
```

### Build from Source

```sh
make                  # builds lib/liblamina.so
make test             # runs the C unit test suite
make npm-test         # builds and runs the Node.js / WASM test suite
make examples         # builds example programs in examples/
sudo make install     # installs headers to /usr/local/include/ttypt and lib to /usr/local/lib
```

---

## 8. JavaScript & WebAssembly Bindings (`@tty-pt/lamina`)

For browser, Node.js, and WebGL2 applications, `liblamina` ships a full TypeScript, WebGL2 bridge, and WebAssembly package on npm:

```sh
npm install @tty-pt/lamina
```

See [npm/README.md](npm/README.md) for the full TypeScript, WebGL2 renderer, and WebAssembly API reference.

---

## License

Mostly BSD 2-Clause License. Copyright (c) 2025–2026, Paulo André Azevedo Quirino. See `LICENSE`. The Linux framebuffer device initialization is derived from [pedroth's linux-framebuffer](https://github.com/pedroth/linux-framebuffer) and remains under the Apache License, Version 2.0.
