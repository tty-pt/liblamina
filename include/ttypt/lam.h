/**
 * @file lam.h
 * @brief Public header for the liblamina rendering and input system.
 *
 * liblamina is a lightweight, cross-platform 2D graphics layer providing
 * a unified API across OpenGL, framebuffer, and software backends.
 *
 * It manages window/framebuffer creation, texture and sprite
 * drawing, color and tint management, and basic input event handling.
 */

#ifndef LAM_H
#define LAM_H

#include <stdint.h>
#include <stddef.h>
#include "./lam-key.h"

#ifdef __cplusplus
extern "C" {
#endif

/*───────────────────────────────────────────────*
 *              CONSTANTS & TYPES                *
 *───────────────────────────────────────────────*/

/**
 * @defgroup lam_types liblamina types and constants
 * @brief Fundamental types and constants used by liblamina.
 * @{
 */

/** Pack 8-bit RGBA channels into 32-bit integer compatible with liblamina shaders. */
#define LAM_RGBA(r, g, b, a) \
	((((uint32_t)(a) & 0xFF) << 24) | \
	 (((uint32_t)(r) & 0xFF) << 16) | \
	 (((uint32_t)(g) & 0xFF) << 8)  | \
	  ((uint32_t)(b) & 0xFF))

/** Pack 8-bit RGB channels with full opacity (alpha = 255). */
#define LAM_RGB(r, g, b) LAM_RGBA(r, g, b, 255)

/** Extract 8-bit Alpha channel from 32-bit color. */
#define LAM_COLOR_A(c) (((uint32_t)(c) >> 24) & 0xFF)
/** Extract 8-bit Red channel from 32-bit color. */
#define LAM_COLOR_R(c) (((uint32_t)(c) >> 16) & 0xFF)
/** Extract 8-bit Green channel from 32-bit color. */
#define LAM_COLOR_G(c) (((uint32_t)(c) >> 8) & 0xFF)
/** Extract 8-bit Blue channel from 32-bit color. */
#define LAM_COLOR_B(c) ((uint32_t)(c) & 0xFF)

/** Standard color palette constants */
#define LAM_COLOR_WHITE        0xFFFFFFFF
#define LAM_COLOR_BLACK        0xFF000000
#define LAM_COLOR_TRANSPARENT  0x00000000
#define LAM_COLOR_RED          0xFFFF0000
#define LAM_COLOR_GREEN        0xFF00FF00
#define LAM_COLOR_BLUE         0xFF0000FF
#define LAM_COLOR_YELLOW       0xFFFFFF00
#define LAM_COLOR_CYAN         0xFF00FFFF
#define LAM_COLOR_MAGENTA      0xFFFF00FF

/** @brief Input field flags used by keyboard handlers. */
enum lam_key_flags {
	LAM_INPUT_MULTILINE = 1, /**< Allows multi-line input (ENTER adds newlines). */
	LAM_INPUT_NUMERIC   = 2, /**< Restricts input to numeric characters. */
};

/**
 * @brief Software rendering callback.
 *
 * Called once per pixel for procedural or CPU-based rendering.
 *
 * @param[out] color Pointer to the pixel in BGRA format.
 * @param[in]  x     X coordinate of the pixel.
 * @param[in]  y     Y coordinate of the pixel.
 * @param[in]  ctx   Optional user data pointer.
 */
typedef void lam_lambda_t(uint8_t *color,
                          uint32_t x, uint32_t y,
                          void *ctx);

/**
 * @brief Obtain the current framebuffer dimensions.
 *
 * Retrieves the logical pixel dimensions of the active render target
 * (window, screen, or offscreen buffer).
 *
 * @param[out] width  Framebuffer width in pixels.
 * @param[out] height Framebuffer height in pixels.
 */
void lam_size(uint32_t *width, uint32_t *height);

/**
 * @brief Initialise the OpenGL context, window, and input system.
 *
 * Must be called once by the application after the library is loaded,
 * before any rendering or input functions are used.
 */
void lam_init(void);
void lam_deinit(void);

/** Default white RGBA tint (no color modulation). */
static const uint32_t lam_default_tint = 0xFFFFFFFF;

/**
 * @brief Keyboard event callback prototype.
 *
 * @param[in] code  Platform key code.
 * @param[in] type  Event type (press, release, repeat).
 * @param[in] value Event state (1 = pressed, 0 = released).
 * @return Non-zero to consume the event, zero to propagate.
 */
typedef int lam_key_cb_t(unsigned short code,
                         unsigned short type,
                         int value);

/** @} */


/*───────────────────────────────────────────────*
 *                 RENDERING API                 *
 *───────────────────────────────────────────────*/

/**
 * @defgroup lam_render liblamina rendering
 * @brief Immediate-mode drawing and frame presentation.
 * @{
 */

/**
 * @brief Render a region using a user-defined callback.
 *
 * Executes a callback per pixel, useful for procedural effects
 * or software rendering directly into the framebuffer.
 *
 * @param[in] lambda Per-pixel callback function.
 * @param[in] x,y    Region origin.
 * @param[in] w,h    Region size in pixels.
 * @param[in] ctx    Optional user context.
 */
void lam_render(lam_lambda_t *lambda,
                int32_t x, int32_t y,
                uint32_t w, uint32_t h,
                void *ctx);

/**
 * @brief Draw a solid-colored rectangle.
 *
 * Fills a rectangular region with the specified color.
 *
 * @param[in] x,y   Position in pixels.
 * @param[in] w,h   Dimensions.
 * @param[in] color 32-bit RGBA color.
 */
void lam_fill(int32_t x, int32_t y,
              uint32_t w, uint32_t h,
              uint32_t color);

/**
 * @brief Clear the entire current framebuffer/viewport with a background color.
 *
 * @param[in] color 32-bit packed color (LAM_RGBA / LAM_RGB).
 */
void lam_clear(uint32_t color);

/**
 * @brief Draw a hollow rectangle outline.
 *
 * @param[in] x,y          Position in pixels.
 * @param[in] w,h          Dimensions in pixels.
 * @param[in] border_color 32-bit packed border color.
 * @param[in] border_width Border thickness in pixels.
 */
void lam_rect(int32_t x, int32_t y,
              uint32_t w, uint32_t h,
              uint32_t border_color,
              uint32_t border_width);

/**
 * @brief Draw a line segment between two points.
 *
 * @param[in] x0,y0     Start coordinate in pixels.
 * @param[in] x1,y1     End coordinate in pixels.
 * @param[in] color     32-bit packed color.
 * @param[in] thickness Line thickness in pixels.
 */
void lam_line(int32_t x0, int32_t y0,
              int32_t x1, int32_t y1,
              uint32_t color,
              uint32_t thickness);

/**
 * @brief Present the current framebuffer.
 *
 * Flushes pending draw commands and updates the visible frame
 * through the active backend (OpenGL swap, FB copy, etc.).
 */
void lam_flush(void);

/** @} */


/*───────────────────────────────────────────────*
 *              TEXTURE / SPRITE API             *
 *───────────────────────────────────────────────*/

/**
 * @defgroup lam_texture liblamina textures and sprites
 * @brief Texture management and sprite drawing.
 * @{
 */

/**
 * @brief Draw a subregion of a texture to screen.
 *
 * @param[in] ref Texture reference ID.
 * @param[in] x,y Destination position.
 * @param[in] cx,cy Source region origin.
 * @param[in] sw,sh Source region size.
 * @param[in] dw,dh Destination dimensions.
 * @param[in] tint RGBA tint multiplier.
 */
void lam_tex_draw_x(uint32_t ref,
                    int32_t x, int32_t y,
                    uint32_t cx, uint32_t cy,
                    uint32_t sw, uint32_t sh,
                    uint32_t dw, uint32_t dh,
                    uint32_t tint);

/**
 * @brief Draw an entire texture scaled to fit a rectangle.
 *
 * @param[in] ref Texture reference ID.
 * @param[in] x,y Destination position.
 * @param[in] dw,dh Destination dimensions.
 */
void lam_tex_draw(uint32_t ref,
                  int32_t x, int32_t y,
                  uint32_t dw, uint32_t dh);

/**
 * @brief Load an image file into a texture.
 *
 * @param[in] filename Path to the image file.
 * @return Texture reference ID.
 */
unsigned lam_tex_load(const char *filename);

/**
 * @brief Save a texture to disk (if supported by backend).
 *
 * @param[in] ref Texture reference ID.
 */
void lam_tex_save(unsigned ref);

/**
 * @brief Get the pixel dimensions of a texture.
 *
 * @param[out] w Width in pixels.
 * @param[out] h Height in pixels.
 * @param[in]  ref Texture reference ID.
 */
void lam_tex_size(uint32_t *w, uint32_t *h, unsigned ref);

/**
 * @brief Read a pixel color from a texture.
 *
 * @param[in] ref Texture reference ID.
 * @param[in] x,y Pixel coordinates.
 * @return 32-bit BGRA color value.
 */
uint32_t lam_tex_pick(unsigned ref, uint32_t x, uint32_t y);

/**
 * @brief Write a single pixel into a texture.
 *
 * @param[in] ref Texture reference ID.
 * @param[in] x,y Target pixel coordinates.
 * @param[in] color 32-bit BGRA color.
 */
void lam_tex_paint(unsigned ref,
                   uint32_t x, uint32_t y,
                   uint32_t color);

/**
 * @brief Apply a global color tint to future draw calls.
 *
 * @param[in] tint RGBA color multiplier.
 */
void lam_tint(uint32_t tint);

/** @} */


/*───────────────────────────────────────────────*
 *                 INPUT API                     *
 *───────────────────────────────────────────────*/

/**
 * @defgroup lam_input liblamina input handling
 * @brief Keyboard polling and event dispatch.
 * @{
 */

/**
 * @brief Poll and process pending input events.
 *
 * Backend-specific function that updates keyboard state
 * and invokes registered key callbacks.
 */
void lam_poll(void);

/**
 * @brief Register a callback for a specific key.
 *
 * @param[in] key Key code (platform dependent).
 * @param[in] cb  Callback function.
 */
void lam_key_reg(unsigned short key, lam_key_cb_t *cb);

/**
 * @brief Register a global keyboard callback.
 *
 * Called for all key events that aren’t handled by specific bindings.
 *
 * @param[in] cb Callback function.
 */
void lam_key_default_reg(lam_key_cb_t *cb);

/**
 * @brief Translate a key code into its character value.
 *
 * @param[in] code  Key code.
 * @return          Character code or 0 if not printable.
 */
unsigned short lam_key_val(unsigned short code);

/**
 * @brief Append a character based on a key press into a string buffer.
 *
 * @param[out] target Output text buffer.
 * @param[in]  len    Current string length.
 * @param[in]  code   Key code.
 * @param[in]  flags  Input flags (e.g., numeric, multiline).
 * @return            Number of bytes written.
 */
int lam_key_parse(char *target, size_t len,
                  unsigned short code, int flags);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LAM_H */
