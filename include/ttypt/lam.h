#ifndef TTYPT_LAM_H
#define TTYPT_LAM_H

/*
 * lam.h — Canonical 2D immediate-mode graphics and tilemap interface.
 * Implements two-level WASM import namespace "lamina" with native fallback.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

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
#else
static inline void lam_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) { (void)x; (void)y; (void)w; (void)h; (void)color; }
static inline void lam_tile_draw(uint32_t ref, uint32_t idx, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rx, uint32_t ry) { (void)ref; (void)idx; (void)x; (void)y; (void)w; (void)h; (void)rx; (void)ry; }
static inline void lam_flush(void) {}
static inline void lam_canvas_flush(const void *buf, uint32_t w, uint32_t h) { (void)buf; (void)w; (void)h; }
static inline uint32_t lam_shader_register(const char *name, const char *frag_glsl) { (void)name; (void)frag_glsl; return 0; }
static inline void lam_shader_bind(uint32_t shader_id) { (void)shader_id; }
static inline void lam_shader_uniforms(uint32_t shader_id, const float *values, uint32_t count) { (void)shader_id; (void)values; (void)count; }
static inline void lam_cam_set_3d(float tx, float ty, float tz, float d, float p, float y, float f)
{ (void)tx; (void)ty; (void)tz; (void)d; (void)p; (void)y; (void)f; }
static inline void lam_cam_set_2d(void) {}
static inline void lam_terrain_mesh_flush(const void *c, const int16_t *h,
                                         uint32_t cw, uint32_t ch,
                                         uint32_t sw, uint32_t sh,
                                         float sl, const float *cp,
                                         const void *water_rgba)
{ (void)c; (void)h; (void)cw; (void)ch; (void)sw; (void)sh; (void)sl; (void)cp; (void)water_rgba; }
static inline void lam_billboard_batch_flush(const void *b, uint32_t count, const void *atlas)
{ (void)b; (void)count; (void)atlas; }
#endif

#ifdef __cplusplus
}
#endif

#endif /* TTYPT_LAM_H */
