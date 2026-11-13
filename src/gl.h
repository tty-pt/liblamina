/*
 * @file gl.h
 * @brief Internal OpenGL utility layer for the liblamina engine.
 */

#ifndef LAM_GL_H
#define LAM_GL_H

#define GL_GLEXT_PROTOTYPES 1
#define GLEW_STATIC

#ifdef __APPLE__
# define GL_SILENCE_DEPRECATION
# include <OpenGL/glext.h>
# include <OpenGL/gl3.h>
#else
# include <GL/glew.h>
# include <GL/gl.h>
#endif

#include <stdint.h>

/* Off-screen framebuffer object used as the engine’s main render target. */
extern GLuint g_fbo;

/* Dummy vertex array (required by core profile). */
extern GLuint g_vao_dummy;

/* Uniform locations for the texture drawing program. */
extern GLint g_uProj_tex;
extern GLint g_uDst_tex;
extern GLint g_uUV_tex;
extern GLint g_uTint_tex;
extern GLint g_uSampler;

/* Active texture drawing shader program. */
extern GLuint g_prog_tex;

/* Global orthographic projection matrix. */
extern float lam_ortho_M[16];

/* 3D Depth buffer and pipeline */
extern GLuint g_rbo_depth;
extern GLuint g_prog_3d;
extern GLint g_uMVP_3d;
extern GLint g_uV0_3d, g_uV1_3d, g_uV2_3d, g_uV3_3d;
extern GLint g_uUV_3d;
extern GLint g_uTint_3d;
extern GLint g_uSampler_3d;

/* Vertex shader source for solid-color fills. */
extern const char *VS_FILL;

void lam_ortho(float w, float h, float *m);
GLuint lam_link(GLuint vs, GLuint fs);
GLuint lam_compile(GLenum type, const char *src);
void lam_set_viewport(GLuint fbo, uint32_t w, uint32_t h);
void lam_reset_viewport(void);
void lam_apply_ortho(GLint uProj);
GLuint lam_current_fbo(void);

void lam_tex_draw_quad_3d(uint32_t ref,
                          const float v0[3], const float v1[3],
                          const float v2[3], const float v3[3],
                          uint32_t cx, uint32_t cy,
                          uint32_t sw, uint32_t sh,
                          uint32_t tint);

#endif /* LAM_GL_H */
