#ifndef TTYPT_WEBGL_H
#define TTYPT_WEBGL_H

/*
 * webgl.h — Canonical WebGL 1/2 graphics interface for WebAssembly and native C.
 * Implements two-level WASM import namespace "webgl" with native fallback.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef float GLclampf;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;

#define GL_FALSE                  0
#define GL_TRUE                   1

#define GL_COLOR_BUFFER_BIT       0x00004000
#define GL_DEPTH_BUFFER_BIT       0x00000100

#define GL_POINTS                 0x0000
#define GL_LINES                  0x0001
#define GL_TRIANGLES              0x0004
#define GL_TRIANGLE_STRIP         0x0005
#define GL_TRIANGLE_FAN           0x0006

#define GL_BYTE                   0x1400
#define GL_UNSIGNED_BYTE          0x1401
#define GL_SHORT                  0x1402
#define GL_UNSIGNED_SHORT         0x1403
#define GL_INT                    0x1404
#define GL_UNSIGNED_INT           0x1405
#define GL_FLOAT                  0x1406

#define GL_FRAGMENT_SHADER        0x8B30
#define GL_VERTEX_SHADER          0x8B31

#define GL_ARRAY_BUFFER           0x8892
#define GL_ELEMENT_ARRAY_BUFFER   0x8893

#define GL_STREAM_DRAW            0x88E0
#define GL_STATIC_DRAW            0x88E4
#define GL_DYNAMIC_DRAW           0x88E8

#define GL_COMPILE_STATUS         0x8B81
#define GL_LINK_STATUS            0x8B82
#define GL_COMPLETION_STATUS_KHR  0x91B1

#define GL_DEPTH_TEST             0x0B71
#define GL_BLEND                  0x0BE2
#define GL_CULL_FACE              0x0B44
#define GL_LESS                   0x0201
#define GL_LEQUAL                 0x0203
#define GL_ZERO                   0
#define GL_ONE                    1
#define GL_SRC_ALPHA              0x0302
#define GL_ONE_MINUS_SRC_ALPHA    0x0303
#define GL_TEXTURE_2D             0x0DE1
#define GL_TEXTURE_2D_ARRAY       0x8C1A
#define GL_TEXTURE0               0x84C0
#define GL_TEXTURE1               0x84C1
#define GL_TEXTURE2               0x84C2
#define GL_TEXTURE_MAG_FILTER     0x2800
#define GL_TEXTURE_MIN_FILTER     0x2801
#define GL_TEXTURE_WRAP_S         0x2802
#define GL_TEXTURE_WRAP_T         0x2803
#define GL_NEAREST                0x2600
#define GL_LINEAR                 0x2601
#define GL_CLAMP_TO_EDGE          0x812F
#define GL_RGBA                   0x1908
#define GL_R16I                   0x8233
#define GL_RED_INTEGER            0x8D94
#define GL_UNPACK_ALIGNMENT       0x0CF5
/* Offscreen render targets (WebGL2): framebuffers, renderbuffers, resolve blits */
#define GL_FRAMEBUFFER            0x8D40
#define GL_READ_FRAMEBUFFER       0x8CA8
#define GL_DRAW_FRAMEBUFFER       0x8CA9
#define GL_RENDERBUFFER           0x8D41
#define GL_COLOR_ATTACHMENT0      0x8CE0
#define GL_DEPTH_ATTACHMENT       0x8D00
#define GL_FRAMEBUFFER_COMPLETE   0x8CD5
#define GL_RGBA8                  0x8058
#define GL_RGBA16F                0x881A
#define GL_HALF_FLOAT             0x140B
#define GL_R8                     0x8229
#define GL_RED                    0x1903
#define GL_DEPTH_COMPONENT        0x1902
#define GL_DEPTH_COMPONENT32F     0x8CAC
#define GL_DEPTH_STENCIL          0x84F9
#define GL_DEPTH32F_STENCIL8      0x8CAD
#define GL_FLOAT_32_UNSIGNED_INT_24_8_REV 0x8DAD
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_STENCIL_TEST           0x0B90
#define GL_STENCIL_BUFFER_BIT     0x00000400
#define GL_ALWAYS                 0x0207
#define GL_EQUAL                  0x0202
#define GL_KEEP                   0x1E00
#define GL_REPLACE                0x1E01

#define GL_POLYGON_OFFSET_FILL    0x8037

#ifdef __wasm__
__attribute__((import_module("webgl"), import_name("glViewport")))
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);

__attribute__((import_module("webgl"), import_name("glClearColor")))
void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);

__attribute__((import_module("webgl"), import_name("glClear")))
void glClear(GLenum mask);

__attribute__((import_module("webgl"), import_name("glEnable")))
void glEnable(GLenum cap);

__attribute__((import_module("webgl"), import_name("glDisable")))
void glDisable(GLenum cap);

__attribute__((import_module("webgl"), import_name("glDepthFunc")))
void glDepthFunc(GLenum func);

__attribute__((import_module("webgl"), import_name("glDepthMask")))
void glDepthMask(GLboolean flag);

__attribute__((import_module("webgl"), import_name("glBlendFunc")))
void glBlendFunc(GLenum sfactor, GLenum dfactor);

__attribute__((import_module("webgl"), import_name("glColorMask")))
void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);

__attribute__((import_module("webgl"), import_name("glPolygonOffset")))
void glPolygonOffset(GLfloat factor, GLfloat units);

__attribute__((import_module("webgl"), import_name("glCreateShader")))
GLuint glCreateShader(GLenum type);

__attribute__((import_module("webgl"), import_name("glShaderSource")))
void glShaderSource(GLuint shader, const char *src, size_t len);

__attribute__((import_module("webgl"), import_name("glCompileShader")))
void glCompileShader(GLuint shader);

__attribute__((import_module("webgl"), import_name("glGetShaderParameter")))
GLint glGetShaderParameter(GLuint shader, GLenum pname);

__attribute__((import_module("webgl"), import_name("glCreateProgram")))
GLuint glCreateProgram(void);

__attribute__((import_module("webgl"), import_name("glAttachShader")))
void glAttachShader(GLuint program, GLuint shader);

__attribute__((import_module("webgl"), import_name("glLinkProgram")))
void glLinkProgram(GLuint program);

__attribute__((import_module("webgl"), import_name("glGetProgramParameter")))
GLint glGetProgramParameter(GLuint program, GLenum pname);

__attribute__((import_module("webgl"), import_name("glUseProgram")))
void glUseProgram(GLuint program);

__attribute__((import_module("webgl"), import_name("glCreateBuffer")))
GLuint glCreateBuffer(void);

__attribute__((import_module("webgl"), import_name("glBindBuffer")))
void glBindBuffer(GLenum target, GLuint buffer);

__attribute__((import_module("webgl"), import_name("glBufferData")))
void glBufferData(GLenum target, const void *data, size_t size, GLenum usage);

__attribute__((import_module("webgl"), import_name("glBufferSubData")))
void glBufferSubData(GLenum target, size_t offset, size_t size, const void *data);

__attribute__((import_module("webgl"), import_name("glCreateTexture")))
GLuint glCreateTexture(void);

__attribute__((import_module("webgl"), import_name("glDeleteTexture")))
void glDeleteTexture(GLuint texture);

__attribute__((import_module("webgl"), import_name("glBindTexture")))
void glBindTexture(GLenum target, GLuint texture);

__attribute__((import_module("webgl"), import_name("glActiveTexture")))
void glActiveTexture(GLenum texture);

__attribute__((import_module("webgl"), import_name("glTexParameteri")))
void glTexParameteri(GLenum target, GLenum pname, GLint param);

__attribute__((import_module("webgl"), import_name("glPixelStorei")))
void glPixelStorei(GLenum pname, GLint param);

__attribute__((import_module("webgl"), import_name("glTexImage2D")))
void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const void *pixels);

__attribute__((import_module("webgl"), import_name("glTexSubImage2D")))
void glTexSubImage2D(GLenum target, GLint level,
                     GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height,
                     GLenum format, GLenum type, const void *pixels);

__attribute__((import_module("webgl"), import_name("glTexImage3D")))
void glTexImage3D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLsizei depth,
                  GLint border, GLenum format, GLenum type, const void *pixels);

__attribute__((import_module("webgl"), import_name("glTexSubImage3D")))
void glTexSubImage3D(GLenum target, GLint level,
                     GLint xoffset, GLint yoffset, GLint zoffset,
                     GLsizei width, GLsizei height, GLsizei depth,
                     GLenum format, GLenum type, const void *pixels);

__attribute__((import_module("webgl"), import_name("glEnableVertexAttribArray")))
void glEnableVertexAttribArray(GLuint index);

__attribute__((import_module("webgl"), import_name("glDisableVertexAttribArray")))
void glDisableVertexAttribArray(GLuint index);

__attribute__((import_module("webgl"), import_name("glVertexAttribPointer")))
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, size_t offset);

__attribute__((import_module("webgl"), import_name("glDrawArrays")))
void glDrawArrays(GLenum mode, GLint first, GLsizei count);

__attribute__((import_module("webgl"), import_name("glDrawElements")))
void glDrawElements(GLenum mode, GLsizei count, GLenum type, size_t offset);

__attribute__((import_module("webgl"), import_name("glDrawElementsInstanced")))
void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, size_t offset, GLsizei instancecount);

__attribute__((import_module("webgl"), import_name("glDrawArraysInstanced")))
void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount);

__attribute__((import_module("webgl"), import_name("glVertexAttribDivisor")))
void glVertexAttribDivisor(GLuint index, GLuint divisor);

__attribute__((import_module("webgl"), import_name("glGetAttribLocation")))
GLint glGetAttribLocation(GLuint program, const char *name, size_t len);

__attribute__((import_module("webgl"), import_name("glBindAttribLocation")))
void glBindAttribLocation(GLuint program, GLuint index, const char *name, size_t len);

__attribute__((import_module("webgl"), import_name("glGetUniformLocation")))
GLint glGetUniformLocation(GLuint program, const char *name, size_t len);

__attribute__((import_module("webgl"), import_name("glUniform1i")))
void glUniform1i(GLint location, GLint v0);

__attribute__((import_module("webgl"), import_name("glUniform1f")))
void glUniform1f(GLint location, GLfloat v0);

__attribute__((import_module("webgl"), import_name("glUniform2f")))
void glUniform2f(GLint location, GLfloat v0, GLfloat v1);

__attribute__((import_module("webgl"), import_name("glUniform3f")))
void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);

__attribute__((import_module("webgl"), import_name("glUniform4f")))
void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);

__attribute__((import_module("webgl"), import_name("glUniform2fv")))
void glUniform2fv(GLint location, GLsizei count, const GLfloat *value);

__attribute__((import_module("webgl"), import_name("glUniform3fv")))
void glUniform3fv(GLint location, GLsizei count, const GLfloat *value);

__attribute__((import_module("webgl"), import_name("glUniformMatrix4fv")))
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

__attribute__((import_module("webgl"), import_name("glDeleteShader")))
void glDeleteShader(GLuint shader);

__attribute__((import_module("webgl"), import_name("glDeleteProgram")))
void glDeleteProgram(GLuint program);

__attribute__((import_module("webgl"), import_name("glDeleteBuffer")))
void glDeleteBuffer(GLuint buffer);

__attribute__((import_module("webgl"), import_name("glCreateFramebuffer")))
GLuint glCreateFramebuffer(void);

__attribute__((import_module("webgl"), import_name("glDeleteFramebuffer")))
void glDeleteFramebuffer(GLuint fb);

__attribute__((import_module("webgl"), import_name("glBindFramebuffer")))
void glBindFramebuffer(GLenum target, GLuint fb);

__attribute__((import_module("webgl"), import_name("glFramebufferTexture2D")))
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint tex, GLint level);

__attribute__((import_module("webgl"), import_name("glFramebufferRenderbuffer")))
void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum rbtarget, GLuint rb);

__attribute__((import_module("webgl"), import_name("glCheckFramebufferStatus")))
GLenum glCheckFramebufferStatus(GLenum target);

__attribute__((import_module("webgl"), import_name("glCreateRenderbuffer")))
GLuint glCreateRenderbuffer(void);

__attribute__((import_module("webgl"), import_name("glDeleteRenderbuffer")))
void glDeleteRenderbuffer(GLuint rb);

__attribute__((import_module("webgl"), import_name("glBindRenderbuffer")))
void glBindRenderbuffer(GLenum target, GLuint rb);

__attribute__((import_module("webgl"), import_name("glRenderbufferStorageMultisample")))
void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);

__attribute__((import_module("webgl"), import_name("glStencilFunc")))
void glStencilFunc(GLenum func, GLint ref, GLuint mask);

__attribute__((import_module("webgl"), import_name("glStencilOp")))
void glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass);

__attribute__((import_module("webgl"), import_name("glStencilMask")))
void glStencilMask(GLuint mask);

__attribute__((import_module("webgl"), import_name("glClearStencil")))
void glClearStencil(GLint s);

__attribute__((import_module("webgl"), import_name("glBlitFramebuffer")))
void glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter);

#elif defined(LAM_WEBGL_NATIVE)

typedef struct lam_webgl_surface lam_webgl_surface_t;

lam_webgl_surface_t *lam_webgl_surface_create(uint32_t w, uint32_t h);
void lam_webgl_surface_resize(lam_webgl_surface_t *s, uint32_t w, uint32_t h);
void lam_webgl_surface_destroy(lam_webgl_surface_t *s);
uint32_t lam_webgl_surface_tex(const lam_webgl_surface_t *s);
void lam_webgl_surface_size(const lam_webgl_surface_t *s, uint32_t *w, uint32_t *h);
int lam_webgl_surface_read_rgba(const lam_webgl_surface_t *s,
                                uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                void *out_rgba);
void lam_webgl_begin(lam_webgl_surface_t *s);
void lam_webgl_end(void);

void lam_glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void lam_glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
void lam_glClear(GLenum mask);
void lam_glEnable(GLenum cap);
void lam_glDisable(GLenum cap);
void lam_glDepthFunc(GLenum func);
void lam_glDepthMask(GLboolean flag);
void lam_glBlendFunc(GLenum sfactor, GLenum dfactor);
void lam_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void lam_glPolygonOffset(GLfloat factor, GLfloat units);
GLuint lam_glCreateShader(GLenum type);
void lam_glShaderSource(GLuint shader, const char *src, size_t len);
void lam_glCompileShader(GLuint shader);
GLint lam_glGetShaderParameter(GLuint shader, GLenum pname);
GLuint lam_glCreateProgram(void);
void lam_glAttachShader(GLuint program, GLuint shader);
void lam_glLinkProgram(GLuint program);
GLint lam_glGetProgramParameter(GLuint program, GLenum pname);
void lam_glUseProgram(GLuint program);
GLuint lam_glCreateBuffer(void);
void lam_glBindBuffer(GLenum target, GLuint buffer);
void lam_glBufferData(GLenum target, const void *data, size_t size, GLenum usage);
void lam_glBufferSubData(GLenum target, size_t offset, size_t size, const void *data);
GLuint lam_glCreateTexture(void);
void lam_glDeleteTexture(GLuint texture);
void lam_glBindTexture(GLenum target, GLuint texture);
void lam_glActiveTexture(GLenum texture);
void lam_glTexParameteri(GLenum target, GLenum pname, GLint param);
void lam_glPixelStorei(GLenum pname, GLint param);
void lam_glTexImage2D(GLenum target, GLint level, GLint internalformat,
                      GLsizei width, GLsizei height, GLint border,
                      GLenum format, GLenum type, const void *pixels);
void lam_glTexSubImage2D(GLenum target, GLint level,
                         GLint xoffset, GLint yoffset,
                         GLsizei width, GLsizei height,
                         GLenum format, GLenum type, const void *pixels);
void lam_glTexImage3D(GLenum target, GLint level, GLint internalformat,
                      GLsizei width, GLsizei height, GLsizei depth,
                      GLint border, GLenum format, GLenum type, const void *pixels);
void lam_glTexSubImage3D(GLenum target, GLint level,
                         GLint xoffset, GLint yoffset, GLint zoffset,
                         GLsizei width, GLsizei height, GLsizei depth,
                         GLenum format, GLenum type, const void *pixels);
void lam_glEnableVertexAttribArray(GLuint index);
void lam_glDisableVertexAttribArray(GLuint index);
void lam_glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                               GLboolean normalized, GLsizei stride, size_t offset);
void lam_glDrawArrays(GLenum mode, GLint first, GLsizei count);
void lam_glDrawElements(GLenum mode, GLsizei count, GLenum type, size_t offset);
void lam_glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                                 size_t offset, GLsizei instancecount);
void lam_glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                               GLsizei instancecount);
void lam_glVertexAttribDivisor(GLuint index, GLuint divisor);
GLint lam_glGetAttribLocation(GLuint program, const char *name, size_t len);
void lam_glBindAttribLocation(GLuint program, GLuint index, const char *name, size_t len);
GLint lam_glGetUniformLocation(GLuint program, const char *name, size_t len);
void lam_glUniform1i(GLint location, GLint v0);
void lam_glUniform1f(GLint location, GLfloat v0);
void lam_glUniform2f(GLint location, GLfloat v0, GLfloat v1);
void lam_glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
void lam_glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
void lam_glUniform2fv(GLint location, GLsizei count, const GLfloat *value);
void lam_glUniform3fv(GLint location, GLsizei count, const GLfloat *value);
void lam_glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose,
                            const GLfloat *value);
void lam_glDeleteShader(GLuint shader);
void lam_glDeleteProgram(GLuint program);
void lam_glDeleteBuffer(GLuint buffer);
GLuint lam_glCreateFramebuffer(void);
void lam_glDeleteFramebuffer(GLuint fb);
void lam_glBindFramebuffer(GLenum target, GLuint fb);
void lam_glFramebufferTexture2D(GLenum target, GLenum attachment,
                                GLenum textarget, GLuint tex, GLint level);
void lam_glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                                   GLenum rbtarget, GLuint rb);
GLenum lam_glCheckFramebufferStatus(GLenum target);
GLuint lam_glCreateRenderbuffer(void);
void lam_glDeleteRenderbuffer(GLuint rb);
void lam_glBindRenderbuffer(GLenum target, GLuint rb);
void lam_glRenderbufferStorageMultisample(GLenum target, GLsizei samples,
                                          GLenum internalformat,
                                          GLsizei width, GLsizei height);
void lam_glStencilFunc(GLenum func, GLint ref, GLuint mask);
void lam_glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass);
void lam_glStencilMask(GLuint mask);
void lam_glClearStencil(GLint s);
void lam_glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1,
                           GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                           GLbitfield mask, GLenum filter);

#define glViewport                       lam_glViewport
#define glClearColor                     lam_glClearColor
#define glClear                          lam_glClear
#define glEnable                         lam_glEnable
#define glDisable                        lam_glDisable
#define glDepthFunc                      lam_glDepthFunc
#define glDepthMask                      lam_glDepthMask
#define glBlendFunc                      lam_glBlendFunc
#define glColorMask                      lam_glColorMask
#define glPolygonOffset                  lam_glPolygonOffset
#define glCreateShader                   lam_glCreateShader
#define glShaderSource                   lam_glShaderSource
#define glCompileShader                  lam_glCompileShader
#define glGetShaderParameter             lam_glGetShaderParameter
#define glCreateProgram                  lam_glCreateProgram
#define glAttachShader                   lam_glAttachShader
#define glLinkProgram                    lam_glLinkProgram
#define glGetProgramParameter            lam_glGetProgramParameter
#define glUseProgram                     lam_glUseProgram
#define glCreateBuffer                   lam_glCreateBuffer
#define glBindBuffer                     lam_glBindBuffer
#define glBufferData                     lam_glBufferData
#define glBufferSubData                  lam_glBufferSubData
#define glCreateTexture                  lam_glCreateTexture
#define glDeleteTexture                  lam_glDeleteTexture
#define glBindTexture                    lam_glBindTexture
#define glActiveTexture                  lam_glActiveTexture
#define glTexParameteri                  lam_glTexParameteri
#define glPixelStorei                    lam_glPixelStorei
#define glTexImage2D                     lam_glTexImage2D
#define glTexSubImage2D                  lam_glTexSubImage2D
#define glTexImage3D                     lam_glTexImage3D
#define glTexSubImage3D                  lam_glTexSubImage3D
#define glEnableVertexAttribArray        lam_glEnableVertexAttribArray
#define glDisableVertexAttribArray       lam_glDisableVertexAttribArray
#define glVertexAttribPointer            lam_glVertexAttribPointer
#define glDrawArrays                     lam_glDrawArrays
#define glDrawElements                   lam_glDrawElements
#define glDrawElementsInstanced          lam_glDrawElementsInstanced
#define glDrawArraysInstanced            lam_glDrawArraysInstanced
#define glVertexAttribDivisor            lam_glVertexAttribDivisor
#define glGetAttribLocation              lam_glGetAttribLocation
#define glBindAttribLocation             lam_glBindAttribLocation
#define glGetUniformLocation             lam_glGetUniformLocation
#define glUniform1i                      lam_glUniform1i
#define glUniform1f                      lam_glUniform1f
#define glUniform2f                      lam_glUniform2f
#define glUniform3f                      lam_glUniform3f
#define glUniform4f                      lam_glUniform4f
#define glUniform2fv                     lam_glUniform2fv
#define glUniform3fv                     lam_glUniform3fv
#define glUniformMatrix4fv               lam_glUniformMatrix4fv
#define glDeleteShader                   lam_glDeleteShader
#define glDeleteProgram                  lam_glDeleteProgram
#define glDeleteBuffer                   lam_glDeleteBuffer
#define glCreateFramebuffer              lam_glCreateFramebuffer
#define glDeleteFramebuffer              lam_glDeleteFramebuffer
#define glBindFramebuffer                lam_glBindFramebuffer
#define glFramebufferTexture2D           lam_glFramebufferTexture2D
#define glFramebufferRenderbuffer        lam_glFramebufferRenderbuffer
#define glCheckFramebufferStatus         lam_glCheckFramebufferStatus
#define glCreateRenderbuffer             lam_glCreateRenderbuffer
#define glDeleteRenderbuffer             lam_glDeleteRenderbuffer
#define glBindRenderbuffer               lam_glBindRenderbuffer
#define glRenderbufferStorageMultisample lam_glRenderbufferStorageMultisample
#define glStencilFunc                    lam_glStencilFunc
#define glStencilOp                      lam_glStencilOp
#define glStencilMask                    lam_glStencilMask
#define glClearStencil                   lam_glClearStencil
#define glBlitFramebuffer                lam_glBlitFramebuffer

#else

static inline void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) { (void)x; (void)y; (void)width; (void)height; }
static inline void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { (void)r; (void)g; (void)b; (void)a; }
static inline void glClear(GLenum mask) { (void)mask; }
static inline void glEnable(GLenum cap) { (void)cap; }
static inline void glDisable(GLenum cap) { (void)cap; }
static inline void glDepthFunc(GLenum func) { (void)func; }
static inline void glDepthMask(GLboolean flag) { (void)flag; }
static inline void glBlendFunc(GLenum sfactor, GLenum dfactor) { (void)sfactor; (void)dfactor; }
static inline void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { (void)r; (void)g; (void)b; (void)a; }
static inline void glPolygonOffset(GLfloat factor, GLfloat units) { (void)factor; (void)units; }
static inline GLuint glCreateShader(GLenum type) { (void)type; return 1; }
static inline void glShaderSource(GLuint s, const char *src, size_t len) { (void)s; (void)src; (void)len; }
static inline void glCompileShader(GLuint s) { (void)s; }
static inline GLint glGetShaderParameter(GLuint s, GLenum p) { (void)s; (void)p; return 1; }
static inline GLuint glCreateProgram(void) { return 1; }
static inline void glAttachShader(GLuint p, GLuint s) { (void)p; (void)s; }
static inline void glLinkProgram(GLuint p) { (void)p; }
static inline GLint glGetProgramParameter(GLuint p, GLenum param) { (void)p; (void)param; return 1; }
static inline void glUseProgram(GLuint p) { (void)p; }
static inline GLuint glCreateBuffer(void) { return 1; }
static inline void glBindBuffer(GLenum t, GLuint b) { (void)t; (void)b; }
static inline void glBufferData(GLenum t, const void *d, size_t s, GLenum u) { (void)t; (void)d; (void)s; (void)u; }
static inline void glBufferSubData(GLenum t, size_t o, size_t s, const void *d) { (void)t; (void)o; (void)s; (void)d; }
static inline GLuint glCreateTexture(void) { return 1; }
static inline void glDeleteTexture(GLuint t) { (void)t; }
static inline void glBindTexture(GLenum t, GLuint tex) { (void)t; (void)tex; }
static inline void glActiveTexture(GLenum t) { (void)t; }
static inline void glTexParameteri(GLenum t, GLenum p, GLint v) { (void)t; (void)p; (void)v; }
static inline void glPixelStorei(GLenum p, GLint v) { (void)p; (void)v; }
static inline void glTexImage2D(GLenum t, GLint l, GLint inf, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void *px)
{ (void)t; (void)l; (void)inf; (void)w; (void)h; (void)b; (void)f; (void)ty; (void)px; }
static inline void glTexSubImage2D(GLenum t, GLint l, GLint xo, GLint yo, GLsizei w, GLsizei h, GLenum f, GLenum ty, const void *px)
{ (void)t; (void)l; (void)xo; (void)yo; (void)w; (void)h; (void)f; (void)ty; (void)px; }
static inline void glTexImage3D(GLenum t, GLint l, GLint inf, GLsizei w, GLsizei h, GLsizei d, GLint b, GLenum f, GLenum ty, const void *px)
{ (void)t; (void)l; (void)inf; (void)w; (void)h; (void)d; (void)b; (void)f; (void)ty; (void)px; }
static inline void glTexSubImage3D(GLenum t, GLint l, GLint xo, GLint yo, GLint zo, GLsizei w, GLsizei h, GLsizei d, GLenum f, GLenum ty, const void *px)
{ (void)t; (void)l; (void)xo; (void)yo; (void)zo; (void)w; (void)h; (void)d; (void)f; (void)ty; (void)px; }
static inline void glEnableVertexAttribArray(GLuint i) { (void)i; }
static inline void glDisableVertexAttribArray(GLuint i) { (void)i; }
static inline void glVertexAttribPointer(GLuint i, GLint s, GLenum t, GLboolean n, GLsizei st, size_t o) { (void)i; (void)s; (void)t; (void)n; (void)st; (void)o; }
static inline void glDrawArrays(GLenum m, GLint f, GLsizei c) { (void)m; (void)f; (void)c; }
static inline void glDrawElements(GLenum m, GLsizei c, GLenum t, size_t o) { (void)m; (void)c; (void)t; (void)o; }
static inline void glDrawElementsInstanced(GLenum m, GLsizei c, GLenum t, size_t o, GLsizei ic) { (void)m; (void)c; (void)t; (void)o; (void)ic; }
static inline void glDrawArraysInstanced(GLenum m, GLint f, GLsizei c, GLsizei ic) { (void)m; (void)f; (void)c; (void)ic; }
static inline void glVertexAttribDivisor(GLuint i, GLuint d) { (void)i; (void)d; }
static inline GLint glGetAttribLocation(GLuint p, const char *n, size_t l) { (void)p; (void)n; (void)l; return 0; }
static inline void glBindAttribLocation(GLuint p, GLuint i, const char *n, size_t l) { (void)p; (void)i; (void)n; (void)l; }
static inline GLint glGetUniformLocation(GLuint p, const char *n, size_t l) { (void)p; (void)n; (void)l; return 1; }
static inline void glUniform1i(GLint l, GLint v0) { (void)l; (void)v0; }
static inline void glUniform1f(GLint l, GLfloat v0) { (void)l; (void)v0; }
static inline void glUniform2f(GLint l, GLfloat v0, GLfloat v1) { (void)l; (void)v0; (void)v1; }
static inline void glUniform3f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2) { (void)l; (void)v0; (void)v1; (void)v2; }
static inline void glUniform4f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { (void)l; (void)v0; (void)v1; (void)v2; (void)v3; }
static inline void glUniform2fv(GLint l, GLsizei c, const GLfloat *v) { (void)l; (void)c; (void)v; }
static inline void glUniform3fv(GLint l, GLsizei c, const GLfloat *v) { (void)l; (void)c; (void)v; }
static inline void glUniformMatrix4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)l; (void)c; (void)t; (void)v; }
static inline void glDeleteShader(GLuint s) { (void)s; }
static inline void glDeleteProgram(GLuint p) { (void)p; }
static inline void glDeleteBuffer(GLuint b) { (void)b; }
static inline GLuint glCreateFramebuffer(void) { return 1; }
static inline void glDeleteFramebuffer(GLuint fb) { (void)fb; }
static inline void glBindFramebuffer(GLenum target, GLuint fb) { (void)target; (void)fb; }
static inline void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint tex, GLint level) { (void)target; (void)attachment; (void)textarget; (void)tex; (void)level; }
static inline void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum rbtarget, GLuint rb) { (void)target; (void)attachment; (void)rbtarget; (void)rb; }
static inline GLenum glCheckFramebufferStatus(GLenum target) { (void)target; return GL_FRAMEBUFFER_COMPLETE; }
static inline GLuint glCreateRenderbuffer(void) { return 1; }
static inline void glDeleteRenderbuffer(GLuint rb) { (void)rb; }
static inline void glBindRenderbuffer(GLenum target, GLuint rb) { (void)target; (void)rb; }
static inline void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) { (void)target; (void)samples; (void)internalformat; (void)width; (void)height; }
static inline void glStencilFunc(GLenum func, GLint ref, GLuint mask) { (void)func; (void)ref; (void)mask; }
static inline void glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) { (void)sfail; (void)dpfail; (void)dppass; }
static inline void glStencilMask(GLuint mask) { (void)mask; }
static inline void glClearStencil(GLint s) { (void)s; }
static inline void glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter) { (void)sx0; (void)sy0; (void)sx1; (void)sy1; (void)dx0; (void)dy0; (void)dx1; (void)dy1; (void)mask; (void)filter; }

#endif

#ifdef __cplusplus
}
#endif

#endif /* TTYPT_WEBGL_H */
