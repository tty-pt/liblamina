/*
 * test_webgl.c — Standalone unit test for native WebGL2 HAL (Phase 0 + Phase 1):
 * - lam_webgl_surface_t lifecycle & GL state isolation
 * - GLSL ES 3.00 -> GLSL 330 core translation (#version 300 es + precision lines + isampler2D)
 * - R16I odd-width integer texture upload with UNPACK_ALIGNMENT = 1
 * - MSAA renderbuffer FBO -> glBlitFramebuffer to fb=0 (redirected to surface FBO)
 * - Compositing surface texture onto liblamina framebuffer via lam_tex_draw_gl
 * - 2D software canvas flush via lam_canvas_flush / lam_canvas_gl_tex
 */

#include <ttypt/lam.h>
#include <ttypt/webgl.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *VS_ES300 =
"#version 300 es\n"
"precision highp float;\n"
"in vec2 aPos;\n"
"void main() {\n"
"    gl_Position = vec4(aPos, 0.0, 1.0);\n"
"}\n";

static const char *FS_ES300_ISAMP =
"#version 300 es\n"
"precision highp float;\n"
"precision highp int;\n"
"precision highp isampler2D;\n"
"precision highp sampler2D;\n"
"uniform isampler2D uHeightTex;\n"
"out vec4 FragColor;\n"
"void main() {\n"
"    int h = texelFetch(uHeightTex, ivec2(3, 3), 0).r;\n"
"    float r = (h == 1234) ? 1.0 : 0.0;\n"
"    FragColor = vec4(r, 0.5, 0.25, 1.0);\n"
"}\n";

static void test_surface_and_es300_pipeline(void)
{
	lam_webgl_surface_t *surf = lam_webgl_surface_create(64, 64);
	assert(surf != NULL);
	assert(lam_webgl_surface_tex(surf) != 0);

	uint32_t sw = 0, sh = 0;
	lam_webgl_surface_size(surf, &sw, &sh);
	assert(sw == 64 && sh == 64);

	lam_webgl_surface_resize(surf, 96, 80);
	lam_webgl_surface_size(surf, &sw, &sh);
	assert(sw == 96 && sh == 80);

	lam_webgl_begin(surf);

	/* Compile & link ES 3.00 shaders */
	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, VS_ES300, strlen(VS_ES300));
	glCompileShader(vs);
	assert(glGetShaderParameter(vs, GL_COMPILE_STATUS) == GL_TRUE);

	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, FS_ES300_ISAMP, strlen(FS_ES300_ISAMP));
	glCompileShader(fs);
	assert(glGetShaderParameter(fs, GL_COMPILE_STATUS) == GL_TRUE);

	GLuint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glBindAttribLocation(prog, 0, "aPos", 4);
	glLinkProgram(prog);
	assert(glGetProgramParameter(prog, GL_COMPLETION_STATUS_KHR) == 1);
	assert(glGetProgramParameter(prog, GL_LINK_STATUS) == GL_TRUE);
	glDeleteShader(vs);
	glDeleteShader(fs);

	/* Odd-width 7x7 R16I height texture */
	int16_t heights[49];
	memset(heights, 0, sizeof(heights));
	heights[3 * 7 + 3] = 1234;

	GLuint htex = glCreateTexture();
	assert(htex != 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, htex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R16I, 7, 7, 0,
	             GL_RED_INTEGER, GL_SHORT, heights);

	/* Fullscreen quad VBO */
	const float verts[] = {
		-1.f, -1.f,
		 1.f, -1.f,
		 1.f,  1.f,
		-1.f,  1.f,
	};
	GLuint vbo = glCreateBuffer();
	assert(vbo != 0);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, verts, sizeof(verts), GL_STATIC_DRAW);

	/* Offscreen MSAA FBO -> resolve blit to fb=0 (surface FBO) */
	GLuint msaa_fbo = glCreateFramebuffer();
	GLuint msaa_col = glCreateRenderbuffer();
	glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo);
	glBindRenderbuffer(GL_RENDERBUFFER, msaa_col);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, 96, 80);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	                          GL_RENDERBUFFER, msaa_col);
	assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

	glViewport(0, 0, 96, 80);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(prog);
	GLint u_h = glGetUniformLocation(prog, "uHeightTex", 10);
	assert(u_h >= 0);
	glUniform1i(u_h, 1);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), 0);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glDisableVertexAttribArray(0);

	/* Resolve MSAA -> fb=0 (must redirect to surface->fbo!) */
	glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBlitFramebuffer(0, 0, 96, 80, 0, 0, 96, 80, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glDeleteRenderbuffer(msaa_col);
	glDeleteFramebuffer(msaa_fbo);
	glDeleteBuffer(vbo);
	glDeleteTexture(htex);
	glDeleteProgram(prog);

	lam_webgl_end();

	/* Read back center pixel from surface FBO to prove ES 3.00 isampler2D + MSAA blit rendered */
	uint8_t px[4] = {0, 0, 0, 0};
	assert(lam_webgl_surface_read_rgba(surf, 48, 40, 1, 1, px) == 1);
	assert(px[0] >= 250);
	assert(px[1] >= 124 && px[1] <= 132);
	assert(px[2] >= 60  && px[2] <= 68);
	assert(px[3] >= 250);

	/* Composite WebGL surface onto lamina framebuffer and verify state restored */
	lam_clear(0xFF000000u);
	lam_tex_draw_gl(lam_webgl_surface_tex(surf), 10, 10, 96, 80, 1);
	lam_fill(120, 10, 20, 20, 0xFF00FF00u);
	lam_flush();

	lam_webgl_surface_destroy(surf);
	printf("  test_surface_and_es300_pipeline: PASS (pixel=%u,%u,%u,%u)\n",
	       px[0], px[1], px[2], px[3]);
}

static void test_canvas_flush_and_window_api(void)
{
	uint32_t pixels[16 * 16];
	for (int i = 0; i < 16 * 16; i++)
		pixels[i] = 0xFF336699u;

	lam_canvas_flush(pixels, 16, 16);
	uint32_t cw = 0, ch = 0;
	uint32_t tex = lam_canvas_gl_tex(&cw, &ch);
	assert(tex != 0);
	assert(cw == 16 && ch == 16);

	/* Second flush at same dimensions hits glTexSubImage2D fast path */
	lam_canvas_flush(pixels, 16, 16);
	lam_tex_draw_gl(tex, 0, 0, 16, 16, 0);

	lam_set_title("test_webgl");
	assert(lam_running() == 1);
	int32_t mx = -1, my = -1;
	lam_mouse_pos(&mx, &my);
	assert(mx >= 0 && my >= 0);

	printf("  test_canvas_flush_and_window_api: PASS\n");
}

int main(void)
{
	printf("test_webgl:\n");
	lam_init();
	test_surface_and_es300_pipeline();
	test_canvas_flush_and_window_api();
	lam_deinit();
	printf("test_webgl: ALL TESTS PASSED\n");
	return 0;
}
