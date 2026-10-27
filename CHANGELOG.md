## [1.0.0]

<p align="center">
  <img src="https://raw.githubusercontent.com/tty-pt/liblamina/main/logo.png" alt="liblamina logo" width="220" />
</p>

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
