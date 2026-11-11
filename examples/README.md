# liblamina Examples

This directory contains practical examples demonstrating the liblamina library's features.

## Building Examples

From the repository root:

```bash
make examples
```

This will build all example programs in the `examples/` directory.

## Running Examples

Examples must be run with the library path set:

```bash
# Generate fixtures, build examples, then run an example
cd tests && python3 generate_fixtures.py && cd ..
make examples
LD_LIBRARY_PATH=./lib ./examples/01_basic_rendering
```

On macOS use `DYLD_LIBRARY_PATH` instead of `LD_LIBRARY_PATH`:

```bash
DYLD_LIBRARY_PATH=./lib ./examples/01_basic_rendering
```

## Available Examples

### 01_basic_rendering
Introduction to liblamina's basic rendering API (binary: `./examples/01_basic_rendering`):
- Initializing liblamina
- Drawing rectangles with `lam_fill()`
- Using color values (ARGB format)
- Presenting frames with `lam_flush()` and `lam_poll()`

### 02_textures
Working with textures:
- Loading PNG images with `lam_img_open()`
- Drawing textures with `lam_img_draw()`
- Applying color tints to textures
- Texture cleanup

**Requires:** `tests/fixtures/test_texture.png` (binary: `./examples/02_textures`)

### 03_fonts
Bitmap font rendering:
- Loading bitmap font atlases with `lam_font_open()`
- Rendering text at different scales
- Measuring text dimensions
- Text wrapping and overflow handling
- Color tinting text

**Requires:** `tests/fixtures/test_font.png` (binary: `./examples/03_fonts`)

### 04_tilemaps
Tilemap rendering for 2D games:
- Creating tilemaps from PNG atlases
- Drawing individual tiles
- Building tile-based layouts
- Tilemap cleanup

**Requires:** `tests/fixtures/test_tilemap.png` (binary: `./examples/04_tilemaps`)

### 05_ui_layout
UI layout engine (flexbox-like):
- Creating UI trees with `lam_ui_div()`
- Flexbox-style layout with direction, justify, and align properties
- Nested containers
- Querying layout results with accessor functions
- Text rendering in UI contexts

**Requires:** `tests/fixtures/test_font.png` (binary: `./examples/05_ui_layout`)

### 06_ui_advanced
Advanced UI features:
- Stylesheet-based styling with `lam_ui_style_open()`
- Applying styles to UI elements
- Multiple styled elements
- Performance caching with `lam_ui_cache()`
- Combining layout, styling, and rendering

**Requires:** `tests/fixtures/test_font.png` (binary: `./examples/06_ui_advanced`)

## Example Structure

All examples follow a consistent pattern:

1. **Initialization** - Get screen dimensions, load resources
2. **Rendering** - Draw graphics using liblamina APIs
3. **Presentation** - Flush the frame and poll events
4. **Cleanup** - Free resources

## Dependencies

Examples require:
- liblamina (built from this repository)
- Test fixtures from `tests/fixtures/` (generated with `tests/generate_fixtures.py`)

To generate test fixtures:

```bash
cd tests
python3 generate_fixtures.py
```

## Notes

- Examples are designed for clarity and demonstration, not production use
- Most examples render a single frame and exit
- Font and texture examples use simple test assets from the test suite
- UI examples demonstrate the declarative layout and styling system
