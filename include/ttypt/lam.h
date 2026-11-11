/**
 * @file lam.h
 * @brief Public header for the liblamina rendering and input system.
 *
 * liblamina is a lightweight, cross-platform 2D/3D graphics layer providing
 * a unified API across OpenGL, framebuffer, WebGL2/WASM, and software backends.
 *
 * It manages window/framebuffer creation, texture and sprite
 * drawing, color and tint management, 3D terrain/billboard streaming,
 * and basic input event handling.
 */

#ifndef LAM_H
#define LAM_H
#ifndef TTYPT_LAM_H
#define TTYPT_LAM_H

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
                          uint32_t x,
                          uint32_t y,
                          void *ctx);

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

#ifdef __wasm__

__attribute__((import_module("lamina"), import_name("lam_fill")))
void lam_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

__attribute__((import_module("lamina"), import_name("lam_tile_draw")))
void lam_tile_draw(uint32_t ref, uint32_t idx, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rx, uint32_t ry);

__attribute__((import_module("lamina"), import_name("lam_flush")))
void lam_flush(void);

__attribute__((import_module("lamina"), import_name("lam_canvas_flush")))
void lam_canvas_flush(const void *buf, uint32_t w, uint32_t h);

__attribute__((import_module("lamina"), import_name("lam_shader_register")))
uint32_t lam_shader_register(const char *name, const char *frag_glsl);

__attribute__((import_module("lamina"), import_name("lam_shader_bind")))
void lam_shader_bind(uint32_t shader_id);

__attribute__((import_module("lamina"), import_name("lam_shader_uniforms")))
void lam_shader_uniforms(uint32_t shader_id, const float *values, uint32_t count);

__attribute__((import_module("lamina"), import_name("lam_cam_set_3d")))
void lam_cam_set_3d(float target_x, float target_y, float target_z,
                    float distance, float pitch_deg, float yaw_deg, float fov_deg);

__attribute__((import_module("lamina"), import_name("lam_cam_set_2d")))
void lam_cam_set_2d(void);

__attribute__((import_module("lamina"), import_name("lam_terrain_mesh_flush")))
void lam_terrain_mesh_flush(const void *color_rgba, const int16_t *height_i16,
                            uint32_t chunk_w, uint32_t chunk_h,
                            uint32_t stage_w, uint32_t stage_h,
                            float sea_level, const float *cam_params,
                            const void *water_rgba);

__attribute__((import_module("lamina"), import_name("lam_billboard_batch_flush")))
void lam_billboard_batch_flush(const void *billboard_data, uint32_t count, const void *atlas_ptr);

__attribute__((import_module("lamina"), import_name("lam_image_request")))
uint32_t lam_image_request(const char *url);

__attribute__((import_module("lamina"), import_name("lam_image_read")))
uint32_t lam_image_read(uint32_t img_id, void *dst_rgba, uint32_t max_bytes,
                        uint32_t *out_w, uint32_t *out_h);

#else /* !__wasm__ */

/**
 * @brief Obtain the current framebuffer dimensions.
 *
 * @param[out] width  Framebuffer width in pixels.
 * @param[out] height Framebuffer height in pixels.
 */
void lam_size(uint32_t *width, uint32_t *height);

/**
 * @brief Initialise the OpenGL context, window, and input system.
 */
void lam_init(void);
void lam_deinit(void);

/*───────────────────────────────────────────────*
 *                 RENDERING API                 *
 *───────────────────────────────────────────────*/

/**
 * @defgroup lam_render liblamina rendering
 * @brief Immediate-mode drawing and frame presentation.
 * @{
 */

void lam_render(lam_lambda_t *lambda,
                int32_t x, int32_t y,
                uint32_t w, uint32_t h,
                void *ctx);

void lam_fill(int32_t x, int32_t y,
              uint32_t w, uint32_t h,
              uint32_t color);

void lam_clear(uint32_t color);

void lam_rect(int32_t x, int32_t y,
              uint32_t w, uint32_t h,
              uint32_t border_color,
              uint32_t border_width);

void lam_line(int32_t x0, int32_t y0,
              int32_t x1, int32_t y1,
              uint32_t color,
              uint32_t thickness);

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

void lam_tex_draw_x(uint32_t ref,
                    int32_t x, int32_t y,
                    uint32_t cx, uint32_t cy,
                    uint32_t sw, uint32_t sh,
                    uint32_t dw, uint32_t dh,
                    uint32_t tint);

void lam_tex_draw(uint32_t ref,
                  int32_t x, int32_t y,
                  uint32_t dw, uint32_t dh);

unsigned lam_tex_load(const char *filename);
void lam_tex_save(unsigned ref);
void lam_tex_size(uint32_t *w, uint32_t *h, unsigned ref);
uint32_t lam_tex_pick(unsigned ref, uint32_t x, uint32_t y);
void lam_tex_paint(unsigned ref,
                   uint32_t x, uint32_t y,
                   uint32_t color);
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

void lam_poll(void);
void lam_key_reg(unsigned short key, lam_key_cb_t *cb);
void lam_key_default_reg(lam_key_cb_t *cb);
unsigned short lam_key_val(unsigned short code);
int lam_key_parse(char *target, size_t len,
                  unsigned short code, int flags);

/** @} */

/*───────────────────────────────────────────────*
 *         WEBGL / HYBRID SSR FALLBACKS          *
 *───────────────────────────────────────────────*/

static inline void lam_canvas_flush(const void *buf, uint32_t w, uint32_t h)
{ (void)buf; (void)w; (void)h; }

static inline uint32_t lam_shader_register(const char *name, const char *frag_glsl)
{ (void)name; (void)frag_glsl; return 0; }

static inline void lam_shader_bind(uint32_t shader_id)
{ (void)shader_id; }

static inline void lam_shader_uniforms(uint32_t shader_id, const float *values, uint32_t count)
{ (void)shader_id; (void)values; (void)count; }

static inline void lam_terrain_mesh_flush(const void *c, const int16_t *h,
                                          uint32_t cw, uint32_t ch,
                                          uint32_t sw, uint32_t sh,
                                          float sl, const float *cp,
                                          const void *water_rgba)
{ (void)c; (void)h; (void)cw; (void)ch; (void)sw; (void)sh; (void)sl; (void)cp; (void)water_rgba; }

static inline void lam_billboard_batch_flush(const void *b, uint32_t count, const void *atlas)
{ (void)b; (void)count; (void)atlas; }

static inline uint32_t lam_image_request(const char *url)
{ (void)url; return 0; }

static inline uint32_t lam_image_read(uint32_t img_id, void *dst_rgba, uint32_t max_bytes,
                                      uint32_t *out_w, uint32_t *out_h)
{ (void)img_id; (void)dst_rgba; (void)max_bytes; (void)out_w; (void)out_h; return 0; }

static inline void lam_cam_set_3d_7_(float tx, float ty, float tz, float d, float p, float y, float f)
{ (void)tx; (void)ty; (void)tz; (void)d; (void)p; (void)y; (void)f; }

static inline void lam_cam_set_2d_noop_(void) {}

#define _LAM_CAM_SET_3D_PICK(_1, _2, _3, _4, _5, _6, _7, NAME, ...) NAME
#define lam_cam_set_3d(...) _LAM_CAM_SET_3D_PICK(__VA_ARGS__, lam_cam_set_3d_7_, (lam_cam_set_3d))(__VA_ARGS__)

#ifndef LAM_CAM_H
#define lam_cam_set_2d() lam_cam_set_2d_noop_()
#endif

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* TTYPT_LAM_H */
#endif /* LAM_H */
