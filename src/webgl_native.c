/*
 * webgl_native.c — Native desktop OpenGL 3.3 Core backend for <ttypt/webgl.h>
 * and native 2D canvas flush (<ttypt/lam.h>).
 *
 * Isolates WebGL2 state into a lam_webgl_surface_t (FBO + color texture +
 * depth/stencil RBO + core-profile default VAO) so isomorphic WebGL2 C code
 * (e.g. mods/nd/ux/nd_seven_gl*.c) runs unmodified alongside liblamina UI.
 */

#include "./gl.h"
#include "./be.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef GL_COMPLETION_STATUS_KHR
#define GL_COMPLETION_STATUS_KHR 0x91B1
#endif

typedef struct lam_webgl_surface {
	GLuint   fbo;
	GLuint   color_tex;
	GLuint   depth_stencil_rbo;
	GLuint   default_vao;
	uint32_t w;
	uint32_t h;
} lam_webgl_surface_t;

typedef struct {
	GLint     fbo;
	GLint     viewport[4];
	GLint     vao;
	GLint     prog;
	GLboolean depth_test;
	GLboolean blend;
	GLboolean cull_face;
	GLboolean stencil_test;
	GLboolean scissor_test;
	GLboolean poly_offset;
	GLint     active_tex;
	GLint     unpack_align;
} saved_gl_state_t;

static lam_webgl_surface_t *g_active_surface = NULL;
static int                  g_begin_depth = 0;
static GLuint               g_bound_draw_fbo = 0;
static saved_gl_state_t     g_saved_state;

/* Reusable GL texture for lam_canvas_flush() (2D software canvas -> GL) */
static GLuint   g_canvas_tex = 0;
static uint32_t g_canvas_w = 0;
static uint32_t g_canvas_h = 0;

/* -------------------------------------------------------------------------
 * Surface lifecycle & GL state isolation
 * ------------------------------------------------------------------------- */

static void surface_alloc_storage(lam_webgl_surface_t *s, uint32_t w, uint32_t h)
{
	if (w == 0) w = 1;
	if (h == 0) h = 1;

	GLint prev_draw_fbo = 0, prev_read_fbo = 0, prev_tex = 0, prev_rbo = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
	glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev_rbo);

	glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);

	glBindTexture(GL_TEXTURE_2D, s->color_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0,
	             GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	                       GL_TEXTURE_2D, s->color_tex, 0);

	glBindRenderbuffer(GL_RENDERBUFFER, s->depth_stencil_rbo);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
	                      (GLsizei)w, (GLsizei)h);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
	                          GL_RENDERBUFFER, s->depth_stencil_rbo);

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "lam_webgl: surface FBO incomplete (0x%x)\n",
		        (unsigned)status);
	}

	s->w = w;
	s->h = h;

	glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)prev_rbo);
	glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prev_draw_fbo);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_read_fbo);
}

lam_webgl_surface_t *lam_webgl_surface_create(uint32_t w, uint32_t h)
{
	lam_webgl_surface_t *s = calloc(1, sizeof(*s));
	if (!s)
		return NULL;

	glGenFramebuffers(1, &s->fbo);
	glGenTextures(1, &s->color_tex);
	glGenRenderbuffers(1, &s->depth_stencil_rbo);
	glGenVertexArrays(1, &s->default_vao);

	surface_alloc_storage(s, w, h);
	return s;
}

void lam_webgl_surface_resize(lam_webgl_surface_t *s, uint32_t w, uint32_t h)
{
	if (!s || w == 0 || h == 0)
		return;
	if (s->w == w && s->h == h)
		return;
	surface_alloc_storage(s, w, h);
}

void lam_webgl_surface_destroy(lam_webgl_surface_t *s)
{
	if (!s)
		return;
	if (g_active_surface == s) {
		g_active_surface = NULL;
		g_begin_depth = 0;
		g_bound_draw_fbo = 0;
	}
	if (s->default_vao)
		glDeleteVertexArrays(1, &s->default_vao);
	if (s->depth_stencil_rbo)
		glDeleteRenderbuffers(1, &s->depth_stencil_rbo);
	if (s->color_tex)
		glDeleteTextures(1, &s->color_tex);
	if (s->fbo)
		glDeleteFramebuffers(1, &s->fbo);
	free(s);
}

uint32_t lam_webgl_surface_tex(const lam_webgl_surface_t *s)
{
	return s ? (uint32_t)s->color_tex : 0u;
}

void lam_webgl_surface_size(const lam_webgl_surface_t *s, uint32_t *w, uint32_t *h)
{
	if (w) *w = s ? s->w : 0u;
	if (h) *h = s ? s->h : 0u;
}

int lam_webgl_surface_read_rgba(const lam_webgl_surface_t *s,
                                uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                void *out_rgba)
{
	if (!s || !out_rgba || !w || !h)
		return 0;
	if (x + w > s->w || y + h > s->h)
		return 0;

	GLint prev_fbo = 0, prev_pack = 4;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
	glGetIntegerv(GL_PACK_ALIGNMENT, &prev_pack);

	glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels((GLint)x, (GLint)y, (GLsizei)w, (GLsizei)h,
	             GL_RGBA, GL_UNSIGNED_BYTE, out_rgba);

	glPixelStorei(GL_PACK_ALIGNMENT, prev_pack);
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
	return 1;
}

void lam_webgl_begin(lam_webgl_surface_t *s)
{
	if (!s)
		return;

	if (g_begin_depth == 0) {
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &g_saved_state.fbo);
		glGetIntegerv(GL_VIEWPORT, g_saved_state.viewport);
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &g_saved_state.vao);
		glGetIntegerv(GL_CURRENT_PROGRAM, &g_saved_state.prog);
		glGetIntegerv(GL_ACTIVE_TEXTURE, &g_saved_state.active_tex);
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &g_saved_state.unpack_align);
		g_saved_state.depth_test   = glIsEnabled(GL_DEPTH_TEST);
		g_saved_state.blend        = glIsEnabled(GL_BLEND);
		g_saved_state.cull_face    = glIsEnabled(GL_CULL_FACE);
		g_saved_state.stencil_test = glIsEnabled(GL_STENCIL_TEST);
		g_saved_state.scissor_test = glIsEnabled(GL_SCISSOR_TEST);
		g_saved_state.poly_offset  = glIsEnabled(GL_POLYGON_OFFSET_FILL);
	}

	g_begin_depth++;
	g_active_surface = s;
	g_bound_draw_fbo = 0;
	glDisable(GL_SCISSOR_TEST);
	glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
	glBindVertexArray(s->default_vao);
	glViewport(0, 0, (GLsizei)s->w, (GLsizei)s->h);
}

void lam_webgl_end(void)
{
	if (g_begin_depth <= 0)
		return;

	g_begin_depth--;
	if (g_begin_depth > 0)
		return;

	g_active_surface = NULL;
	g_bound_draw_fbo = 0;

	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)g_saved_state.fbo);
	glViewport(g_saved_state.viewport[0], g_saved_state.viewport[1],
	           g_saved_state.viewport[2], g_saved_state.viewport[3]);
	glBindVertexArray((GLuint)g_saved_state.vao);
	glUseProgram((GLuint)g_saved_state.prog);
	glActiveTexture((GLenum)g_saved_state.active_tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, g_saved_state.unpack_align);

	if (g_saved_state.depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (g_saved_state.blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (g_saved_state.cull_face) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (g_saved_state.stencil_test) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
	if (g_saved_state.scissor_test) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
	if (g_saved_state.poly_offset) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL);

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static int g_parallel_compile_probed = 0;
static int g_has_parallel_compile = 0;

static void ensure_parallel_compile_probe(void)
{
	if (g_parallel_compile_probed)
		return;
	g_parallel_compile_probed = 1;
#if defined(GLEW_ARB_parallel_shader_compile)
	if (GLEW_ARB_parallel_shader_compile) {
		g_has_parallel_compile = 1;
		if (glMaxShaderCompilerThreadsARB)
			glMaxShaderCompilerThreadsARB(0xFFFFFFFFu);
		return;
	}
#endif
#if defined(GLEW_KHR_parallel_shader_compile)
	if (GLEW_KHR_parallel_shader_compile) {
		g_has_parallel_compile = 1;
		if (glMaxShaderCompilerThreadsKHR)
			glMaxShaderCompilerThreadsKHR(0xFFFFFFFFu);
		return;
	}
#endif
}

/* -------------------------------------------------------------------------
 * GLSL ES 3.00 -> Desktop GLSL 330 core line-preserving translator
 * ------------------------------------------------------------------------- */

static char *translate_es300_to_gl330(const char *src, size_t len)
{
	/* Output is at most len + 32 bytes (330 core is 2 chars longer than 300 es). */
	char *out = malloc(len + 64);
	if (!out)
		return NULL;

	size_t i = 0, o = 0;
	while (i < len) {
		size_t line_start = i;
		while (i < len && src[i] != '\n')
			i++;
		size_t line_end = i;
		int has_nl = (i < len && src[i] == '\n');
		if (has_nl)
			i++;

		size_t p = line_start;
		while (p < line_end && (src[p] == ' ' || src[p] == '\t' || src[p] == '\r'))
			p++;
		size_t rem = line_end - p;

		if (rem >= 15 && strncmp(src + p, "#version 300 es", 15) == 0) {
			static const char repl[] = "#version 330 core";
			memcpy(out + o, repl, sizeof(repl) - 1);
			o += sizeof(repl) - 1;
		} else {
			/* Strip leading/consecutive `precision ...;` statements while
			 * preserving any remaining code on the same line (minified GLSL). */
			size_t cur = line_start;
			while (cur < line_end) {
				size_t ws = cur;
				while (ws < line_end && (src[ws] == ' ' || src[ws] == '\t' || src[ws] == '\r'))
					ws++;
				if (line_end - ws >= 10 && strncmp(src + ws, "precision ", 10) == 0) {
					size_t semi = ws + 10;
					while (semi < line_end && src[semi] != ';')
						semi++;
					if (semi < line_end && src[semi] == ';') {
						cur = semi + 1;
						continue;
					}
					cur = line_end;
					break;
				}
				break;
			}
			if (cur < line_end) {
				size_t raw_len = line_end - cur;
				memcpy(out + o, src + cur, raw_len);
				o += raw_len;
			}
		}

		if (has_nl)
			out[o++] = '\n';
	}
	out[o] = '\0';
	return out;
}

static const char *copy_slice_z(const char *name, size_t len,
                                char *stack_buf, size_t stack_sz,
                                char **heap_out)
{
	*heap_out = NULL;
	if (len < stack_sz) {
		memcpy(stack_buf, name, len);
		stack_buf[len] = '\0';
		return stack_buf;
	}
	char *h = malloc(len + 1);
	if (!h)
		return "";
	memcpy(h, name, len);
	h[len] = '\0';
	*heap_out = h;
	return h;
}

/* -------------------------------------------------------------------------
 * 63 WebGL2 HAL functions (lam_gl*)
 * ------------------------------------------------------------------------- */

void lam_glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	if (g_active_surface && g_bound_draw_fbo == 0 &&
	    x == 0 && y == 0 && width > 0 && height > 0) {
		uint32_t uw = (uint32_t)width;
		uint32_t uh = (uint32_t)height;
		if (uw != g_active_surface->w || uh != g_active_surface->h)
			surface_alloc_storage(g_active_surface, uw, uh);
	}
	glViewport(x, y, width, height);
}

void lam_glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
	glClearColor(r, g, b, a);
}

void lam_glClear(GLenum mask)
{
	glClear((GLbitfield)mask);
}

void lam_glEnable(GLenum cap)
{
	glEnable(cap);
}

void lam_glDisable(GLenum cap)
{
	glDisable(cap);
}

void lam_glDepthFunc(GLenum func)
{
	glDepthFunc(func);
}

void lam_glDepthMask(GLboolean flag)
{
	glDepthMask(flag);
}

void lam_glBlendFunc(GLenum sfactor, GLenum dfactor)
{
	glBlendFunc(sfactor, dfactor);
}

void lam_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
	glColorMask(r, g, b, a);
}

void lam_glPolygonOffset(GLfloat factor, GLfloat units)
{
	glPolygonOffset(factor, units);
}

GLuint lam_glCreateShader(GLenum type)
{
	return glCreateShader(type);
}

void lam_glShaderSource(GLuint shader, const char *src, size_t len)
{
	if (!src || len == 0)
		return;
	char *translated = translate_es300_to_gl330(src, len);
	if (!translated)
		return;
	const char *ptrs[1] = { translated };
	glShaderSource(shader, 1, ptrs, NULL);
	free(translated);
}

void lam_glCompileShader(GLuint shader)
{
	ensure_parallel_compile_probe();
	glCompileShader(shader);
}

GLint lam_glGetShaderParameter(GLuint shader, GLenum pname)
{
	if (pname == GL_COMPLETION_STATUS_KHR) {
		ensure_parallel_compile_probe();
		if (!g_has_parallel_compile)
			return 1;
	}
	GLint val = 0;
	glGetShaderiv(shader, pname, &val);
	if (pname == GL_COMPILE_STATUS && !val) {
		char log[2048];
		GLsizei n = 0;
		glGetShaderInfoLog(shader, (GLsizei)sizeof(log) - 1, &n, log);
		log[n] = '\0';
		fprintf(stderr, "lam_webgl: shader %u compile error:\n%s\n",
		        (unsigned)shader, log);
	}
	return val;
}

GLuint lam_glCreateProgram(void)
{
	return glCreateProgram();
}

void lam_glAttachShader(GLuint program, GLuint shader)
{
	glAttachShader(program, shader);
}

void lam_glLinkProgram(GLuint program)
{
	ensure_parallel_compile_probe();
	glLinkProgram(program);
}

GLint lam_glGetProgramParameter(GLuint program, GLenum pname)
{
	if (pname == GL_COMPLETION_STATUS_KHR) {
		ensure_parallel_compile_probe();
		if (!g_has_parallel_compile)
			return 1;
	}
	GLint val = 0;
	glGetProgramiv(program, pname, &val);
	if (pname == GL_LINK_STATUS && !val) {
		char log[2048];
		GLsizei n = 0;
		glGetProgramInfoLog(program, (GLsizei)sizeof(log) - 1, &n, log);
		log[n] = '\0';
		fprintf(stderr, "lam_webgl: program %u link error:\n%s\n",
		        (unsigned)program, log);
	}
	return val;
}

void lam_glUseProgram(GLuint program)
{
	glUseProgram(program);
}

GLuint lam_glCreateBuffer(void)
{
	GLuint id = 0;
	glGenBuffers(1, &id);
	return id;
}

void lam_glBindBuffer(GLenum target, GLuint buffer)
{
	glBindBuffer(target, buffer);
}

void lam_glBufferData(GLenum target, const void *data, size_t size, GLenum usage)
{
	glBufferData(target, (GLsizeiptr)size, data, usage);
}

void lam_glBufferSubData(GLenum target, size_t offset, size_t size, const void *data)
{
	glBufferSubData(target, (GLintptr)offset, (GLsizeiptr)size, data);
}

GLuint lam_glCreateTexture(void)
{
	GLuint id = 0;
	glGenTextures(1, &id);
	return id;
}

void lam_glDeleteTexture(GLuint texture)
{
	if (texture)
		glDeleteTextures(1, &texture);
}

void lam_glBindTexture(GLenum target, GLuint texture)
{
	glBindTexture(target, texture);
}

void lam_glActiveTexture(GLenum texture)
{
	glActiveTexture(texture);
}

void lam_glTexParameteri(GLenum target, GLenum pname, GLint param)
{
	glTexParameteri(target, pname, param);
}

void lam_glPixelStorei(GLenum pname, GLint param)
{
	glPixelStorei(pname, param);
}

void lam_glTexImage2D(GLenum target, GLint level, GLint internalformat,
                      GLsizei width, GLsizei height, GLint border,
                      GLenum format, GLenum type, const void *pixels)
{
	glTexImage2D(target, level, internalformat, width, height, border,
	             format, type, pixels);
}

void lam_glTexSubImage2D(GLenum target, GLint level,
                         GLint xoffset, GLint yoffset,
                         GLsizei width, GLsizei height,
                         GLenum format, GLenum type, const void *pixels)
{
	glTexSubImage2D(target, level, xoffset, yoffset, width, height,
	                format, type, pixels);
}

void lam_glTexImage3D(GLenum target, GLint level, GLint internalformat,
                      GLsizei width, GLsizei height, GLsizei depth,
                      GLint border, GLenum format, GLenum type, const void *pixels)
{
	glTexImage3D(target, level, internalformat, width, height, depth,
	             border, format, type, pixels);
}

void lam_glTexSubImage3D(GLenum target, GLint level,
                         GLint xoffset, GLint yoffset, GLint zoffset,
                         GLsizei width, GLsizei height, GLsizei depth,
                         GLenum format, GLenum type, const void *pixels)
{
	glTexSubImage3D(target, level, xoffset, yoffset, zoffset,
	                width, height, depth, format, type, pixels);
}

void lam_glEnableVertexAttribArray(GLuint index)
{
	glEnableVertexAttribArray(index);
}

void lam_glDisableVertexAttribArray(GLuint index)
{
	glDisableVertexAttribArray(index);
}

void lam_glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                               GLboolean normalized, GLsizei stride, size_t offset)
{
	glVertexAttribPointer(index, size, type, normalized, stride,
	                      (const void *)(uintptr_t)offset);
}

void lam_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	glDrawArrays(mode, first, count);
}

void lam_glDrawElements(GLenum mode, GLsizei count, GLenum type, size_t offset)
{
	glDrawElements(mode, count, type, (const void *)(uintptr_t)offset);
}

void lam_glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                                 size_t offset, GLsizei instancecount)
{
	glDrawElementsInstanced(mode, count, type,
	                        (const void *)(uintptr_t)offset, instancecount);
}

void lam_glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                               GLsizei instancecount)
{
	glDrawArraysInstanced(mode, first, count, instancecount);
}

void lam_glVertexAttribDivisor(GLuint index, GLuint divisor)
{
	glVertexAttribDivisor(index, divisor);
}

GLint lam_glGetAttribLocation(GLuint program, const char *name, size_t len)
{
	char stack[256], *heap = NULL;
	const char *z = copy_slice_z(name, len, stack, sizeof(stack), &heap);
	GLint loc = glGetAttribLocation(program, z);
	free(heap);
	return loc;
}

void lam_glBindAttribLocation(GLuint program, GLuint index, const char *name, size_t len)
{
	char stack[256], *heap = NULL;
	const char *z = copy_slice_z(name, len, stack, sizeof(stack), &heap);
	glBindAttribLocation(program, index, z);
	free(heap);
}

GLint lam_glGetUniformLocation(GLuint program, const char *name, size_t len)
{
	char stack[256], *heap = NULL;
	const char *z = copy_slice_z(name, len, stack, sizeof(stack), &heap);
	GLint loc = glGetUniformLocation(program, z);
	free(heap);
	return loc;
}

void lam_glUniform1i(GLint location, GLint v0)
{
	glUniform1i(location, v0);
}

void lam_glUniform1f(GLint location, GLfloat v0)
{
	glUniform1f(location, v0);
}

void lam_glUniform2f(GLint location, GLfloat v0, GLfloat v1)
{
	glUniform2f(location, v0, v1);
}

void lam_glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
	glUniform3f(location, v0, v1, v2);
}

void lam_glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
	glUniform4f(location, v0, v1, v2, v3);
}

void lam_glUniform2fv(GLint location, GLsizei count, const GLfloat *value)
{
	glUniform2fv(location, count, value);
}

void lam_glUniform3fv(GLint location, GLsizei count, const GLfloat *value)
{
	glUniform3fv(location, count, value);
}

void lam_glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose,
                            const GLfloat *value)
{
	glUniformMatrix4fv(location, count, transpose, value);
}

void lam_glDeleteShader(GLuint shader)
{
	glDeleteShader(shader);
}

void lam_glDeleteProgram(GLuint program)
{
	glDeleteProgram(program);
}

void lam_glDeleteBuffer(GLuint buffer)
{
	if (buffer)
		glDeleteBuffers(1, &buffer);
}

GLuint lam_glCreateFramebuffer(void)
{
	GLuint id = 0;
	glGenFramebuffers(1, &id);
	return id;
}

void lam_glDeleteFramebuffer(GLuint fb)
{
	if (fb)
		glDeleteFramebuffers(1, &fb);
}

void lam_glBindFramebuffer(GLenum target, GLuint fb)
{
	if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER)
		g_bound_draw_fbo = fb;
	GLuint actual = (fb == 0 && g_active_surface) ? g_active_surface->fbo : fb;
	glBindFramebuffer(target, actual);
}

void lam_glFramebufferTexture2D(GLenum target, GLenum attachment,
                                GLenum textarget, GLuint tex, GLint level)
{
	glFramebufferTexture2D(target, attachment, textarget, tex, level);
}

void lam_glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                                   GLenum rbtarget, GLuint rb)
{
	glFramebufferRenderbuffer(target, attachment, rbtarget, rb);
}

GLenum lam_glCheckFramebufferStatus(GLenum target)
{
	return glCheckFramebufferStatus(target);
}

GLuint lam_glCreateRenderbuffer(void)
{
	GLuint id = 0;
	glGenRenderbuffers(1, &id);
	return id;
}

void lam_glDeleteRenderbuffer(GLuint rb)
{
	if (rb)
		glDeleteRenderbuffers(1, &rb);
}

void lam_glBindRenderbuffer(GLenum target, GLuint rb)
{
	glBindRenderbuffer(target, rb);
}

void lam_glRenderbufferStorageMultisample(GLenum target, GLsizei samples,
                                          GLenum internalformat,
                                          GLsizei width, GLsizei height)
{
	if (samples <= 0) {
		glRenderbufferStorage(target, internalformat, width, height);
		return;
	}
	GLint max_samples = 4;
	glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
	if (samples > max_samples)
		samples = max_samples;
	glRenderbufferStorageMultisample(target, samples, internalformat, width, height);
}

void lam_glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
	glStencilFunc(func, ref, mask);
}

void lam_glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass)
{
	glStencilOp(sfail, dpfail, dppass);
}

void lam_glStencilMask(GLuint mask)
{
	glStencilMask(mask);
}

void lam_glClearStencil(GLint s)
{
	glClearStencil(s);
}

void lam_glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1,
                           GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                           GLbitfield mask, GLenum filter)
{
	glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
}

void lam_glBindBufferBase(GLenum target, GLuint index, GLuint buffer)
{
	glBindBufferBase(target, index, buffer);
}

GLuint lam_glGetUniformBlockIndex(GLuint program, const char *name, size_t len)
{
	char stack[256], *heap = NULL;
	const char *z = copy_slice_z(name, len, stack, sizeof(stack), &heap);
	GLuint idx = glGetUniformBlockIndex(program, z);
	free(heap);
	return idx;
}

void lam_glUniformBlockBinding(GLuint program, GLuint blockIndex, GLuint blockBinding)
{
	glUniformBlockBinding(program, blockIndex, blockBinding);
}

static inline GLfloat lam_bits_f32(uint32_t u)
{
	union { uint32_t u; GLfloat f; } v;
	v.u = u;
	return v.f;
}

void lam_glCmdFlush(const uint32_t *buf, uint32_t word_count)
{
	if (!buf || !word_count)
		return;
	uint32_t i = 0;
	while (i < word_count) {
		uint32_t op = buf[i++];
		switch (op) {
		case 0u: /* NOP */
			break;
		case 1u: /* USE_PROGRAM */
			if (i + 1u > word_count) return;
			glUseProgram((GLuint)buf[i++]);
			break;
		case 2u: /* ACTIVE_TEXTURE */
			if (i + 1u > word_count) return;
			glActiveTexture((GLenum)buf[i++]);
			break;
		case 3u: /* BIND_TEXTURE */
			if (i + 2u > word_count) return;
			glBindTexture((GLenum)buf[i], (GLuint)buf[i + 1u]);
			i += 2u;
			break;
		case 4u: /* BIND_BUFFER */
			if (i + 2u > word_count) return;
			glBindBuffer((GLenum)buf[i], (GLuint)buf[i + 1u]);
			i += 2u;
			break;
		case 5u: /* ENABLE */
			if (i + 1u > word_count) return;
			glEnable((GLenum)buf[i++]);
			break;
		case 6u: /* DISABLE */
			if (i + 1u > word_count) return;
			glDisable((GLenum)buf[i++]);
			break;
		case 7u: /* DEPTH_MASK */
			if (i + 1u > word_count) return;
			glDepthMask((GLboolean)(buf[i++] ? GL_TRUE : GL_FALSE));
			break;
		case 8u: /* BLEND_FUNC */
			if (i + 2u > word_count) return;
			glBlendFunc((GLenum)buf[i], (GLenum)buf[i + 1u]);
			i += 2u;
			break;
		case 9u: /* UNIFORM1I */
			if (i + 2u > word_count) return;
			glUniform1i((GLint)buf[i], (GLint)buf[i + 1u]);
			i += 2u;
			break;
		case 10u: /* UNIFORM1F */
			if (i + 2u > word_count) return;
			glUniform1f((GLint)buf[i], lam_bits_f32(buf[i + 1u]));
			i += 2u;
			break;
		case 11u: /* UNIFORM2F */
			if (i + 3u > word_count) return;
			glUniform2f((GLint)buf[i], lam_bits_f32(buf[i + 1u]), lam_bits_f32(buf[i + 2u]));
			i += 3u;
			break;
		case 12u: /* UNIFORM3F */
			if (i + 4u > word_count) return;
			glUniform3f((GLint)buf[i], lam_bits_f32(buf[i + 1u]),
			            lam_bits_f32(buf[i + 2u]), lam_bits_f32(buf[i + 3u]));
			i += 4u;
			break;
		case 13u: /* UNIFORM4F */
			if (i + 5u > word_count) return;
			glUniform4f((GLint)buf[i], lam_bits_f32(buf[i + 1u]),
			            lam_bits_f32(buf[i + 2u]), lam_bits_f32(buf[i + 3u]),
			            lam_bits_f32(buf[i + 4u]));
			i += 5u;
			break;
		case 14u: /* UNIFORM_MAT4 */ {
			if (i + 18u > word_count) return;
			GLint loc = (GLint)buf[i];
			GLboolean tr = (GLboolean)(buf[i + 1u] ? GL_TRUE : GL_FALSE);
			GLfloat m[16];
			for (int k = 0; k < 16; k++)
				m[k] = lam_bits_f32(buf[i + 2u + (uint32_t)k]);
			glUniformMatrix4fv(loc, 1, tr, m);
			i += 18u;
			break;
		}
		case 15u: /* VERTEX_ATTRIB_PTR */
			if (i + 6u > word_count) return;
			glVertexAttribPointer((GLuint)buf[i], (GLint)buf[i + 1u], (GLenum)buf[i + 2u],
			                      (GLboolean)(buf[i + 3u] ? GL_TRUE : GL_FALSE),
			                      (GLsizei)buf[i + 4u], (const void *)(uintptr_t)buf[i + 5u]);
			i += 6u;
			break;
		case 16u: /* DRAW_ELEMENTS */
			if (i + 4u > word_count) return;
			glDrawElements((GLenum)buf[i], (GLsizei)buf[i + 1u], (GLenum)buf[i + 2u],
			               (const void *)(uintptr_t)buf[i + 3u]);
			i += 4u;
			break;
		case 17u: /* DRAW_ELEMENTS_INSTANCED */
			if (i + 5u > word_count) return;
			glDrawElementsInstanced((GLenum)buf[i], (GLsizei)buf[i + 1u], (GLenum)buf[i + 2u],
			                        (const void *)(uintptr_t)buf[i + 3u], (GLsizei)buf[i + 4u]);
			i += 5u;
			break;
		case 18u: /* DRAW_ARRAYS */
			if (i + 3u > word_count) return;
			glDrawArrays((GLenum)buf[i], (GLint)buf[i + 1u], (GLsizei)buf[i + 2u]);
			i += 3u;
			break;
		case 19u: /* ENABLE_VAA */
			if (i + 1u > word_count) return;
			glEnableVertexAttribArray((GLuint)buf[i++]);
			break;
		case 20u: /* DISABLE_VAA */
			if (i + 1u > word_count) return;
			glDisableVertexAttribArray((GLuint)buf[i++]);
			break;
		case 21u: /* VERTEX_ATTRIB_DIVISOR */
			if (i + 2u > word_count) return;
			glVertexAttribDivisor((GLuint)buf[i], (GLuint)buf[i + 1u]);
			i += 2u;
			break;
		case 22u: /* BIND_VAO */ {
			if (i + 1u > word_count) return;
			GLuint vao = (GLuint)buf[i++];
			GLuint actual = (vao == 0 && g_active_surface) ? g_active_surface->default_vao : vao;
			glBindVertexArray(actual);
			break;
		}
		default:
			return;
		}
	}
}

GLuint lam_glCreateVertexArray(void)
{
	GLuint id = 0;
	glGenVertexArrays(1, &id);
	return id;
}

void lam_glDeleteVertexArray(GLuint vao)
{
	if (vao)
		glDeleteVertexArrays(1, &vao);
}

void lam_glBindVertexArray(GLuint vao)
{
	GLuint actual = (vao == 0 && g_active_surface) ? g_active_surface->default_vao : vao;
	glBindVertexArray(actual);
}

void lam_glTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat,
                        GLsizei width, GLsizei height, GLsizei depth)
{
#ifndef __APPLE__
	if (glTexStorage3D) {
		glTexStorage3D(target, levels, internalformat, width, height, depth);
		return;
	}
#endif
	GLenum format = GL_RGBA;
	GLenum type = GL_UNSIGNED_BYTE;
	switch (internalformat) {
	case GL_R16I:    format = GL_RED_INTEGER; type = GL_SHORT; break;
	case GL_R16F:    format = GL_RED;         type = GL_FLOAT; break;
	case GL_R8:      format = GL_RED;         type = GL_UNSIGNED_BYTE; break;
	case GL_RGBA16F: format = GL_RGBA;        type = GL_FLOAT; break;
	default:         format = GL_RGBA;        type = GL_UNSIGNED_BYTE; break;
	}
	GLsizei lw = width > 0 ? width : 1;
	GLsizei lh = height > 0 ? height : 1;
	for (GLsizei lvl = 0; lvl < levels; lvl++) {
		glTexImage3D(target, lvl, (GLint)internalformat, lw, lh, depth, 0,
		             format, type, NULL);
		if (lw > 1) lw >>= 1;
		if (lh > 1) lh >>= 1;
	}
	glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, levels > 0 ? levels - 1 : 0);
}

void lam_glGenerateMipmap(GLenum target)
{
	glGenerateMipmap(target);
}

void lam_glTimerBegin(GLuint pass_id)
{
	(void)pass_id;
}

void lam_glTimerEnd(GLuint pass_id)
{
	(void)pass_id;
}

/* -------------------------------------------------------------------------
 * Native 2D software canvas flush -> GL texture (for <canvas> 2D viewports)
 * ------------------------------------------------------------------------- */

void lam_canvas_flush(const void *buf, uint32_t w, uint32_t h)
{
	if (!buf || !w || !h)
		return;

	GLint prev_tex = 0, prev_align = 4;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_align);

	if (!g_canvas_tex) {
		glGenTextures(1, &g_canvas_tex);
		glBindTexture(GL_TEXTURE_2D, g_canvas_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	} else {
		glBindTexture(GL_TEXTURE_2D, g_canvas_tex);
	}

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	if (w != g_canvas_w || h != g_canvas_h) {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0,
		             GL_RGBA, GL_UNSIGNED_BYTE, buf);
		g_canvas_w = w;
		g_canvas_h = h;
	} else {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)w, (GLsizei)h,
		                GL_RGBA, GL_UNSIGNED_BYTE, buf);
	}

	glPixelStorei(GL_UNPACK_ALIGNMENT, prev_align);
	glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);
}

uint32_t lam_canvas_gl_tex(uint32_t *out_w, uint32_t *out_h)
{
	if (out_w) *out_w = g_canvas_w;
	if (out_h) *out_h = g_canvas_h;
	return (uint32_t)g_canvas_tex;
}
