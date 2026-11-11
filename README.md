# liblamina (`<ttypt/lam.h>`)

WebGL2 / Canvas / WebAssembly graphics, viewport, and immediate-mode UI engine in C11 and JavaScript/TypeScript (`@tty-pt/lamina`).

## Overview

`liblamina` provides:
- `<ttypt/lam.h>`, `<ttypt/lam-cam.h>`, `<ttypt/lam-tm.h>`, `<ttypt/lam-ui.h>`, `<ttypt/lam-font.h>`, `<ttypt/lam-key.h>`, and `<ttypt/webgl.h>` headers for 2D/3D viewport, camera, tilemap, and UI layout primitives.
- `htdocs/lamina-bridge.js` and `htdocs/webgl-bridge.js` for browser WebGL2 and Canvas rendering.
- `@tty-pt/lamina` (`npm/`) with compiled WebAssembly (`lamina.wasm`) and TypeScript bindings.

## Build & Test

```sh
make -C external/liblamina
make -C external/liblamina test
```

## License

BSD-2-Clause
