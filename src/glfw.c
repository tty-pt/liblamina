#include "../include/ttypt/lam.h"
#include "./gl.h"
#include "./be.h"

#include <stdlib.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <ttypt/stele.h>

lam_be_t lam_glfw;

static GLuint g_prog_present;
static GLuint g_vao_present;
static GLuint g_vbo_present;

GLFWwindow *g_win;
extern GLuint g_tex;

static const char *VS_PRESENT =
"#version 330 core\n"
"layout (location = 0) in vec2 aPos;\n"
"out vec2 vUV;\n"
"void main(){\n"
"  vUV = aPos * 0.5 + 0.5;\n"
"  gl_Position = vec4(aPos, 0.0, 1.0);\n"
"}\n";

static const char *FS_PRESENT =
"#version 330 core\n"
"in vec2 vUV;\n"
"uniform sampler2D uTex;\n"
"out vec4 FragColor;\n"
"void main(){ FragColor = texture(uTex, vUV); }\n";

static void glfw_init(uint32_t *w, uint32_t *h)
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	g_win = glfwCreateWindow(*w, *h, "be", NULL, NULL);
	CBUG(!g_win, "glfwCreateWindow");

	glfwMakeContextCurrent(g_win);
	glfwSwapInterval(1);

	glfwGetFramebufferSize(g_win, (int32_t *) w, (int32_t *) h);

#ifndef __APPLE__
	glewExperimental = GL_TRUE;
	GLenum gerr = glewInit();
	glGetError(); /* clear any harmless error from glewInit */
	if (gerr != GLEW_OK && !glCreateShader) {
		fprintf(stderr, "glewInit error: %s\n", glewGetErrorString(gerr));
		CBUG(1, "glewInit failed");
	}
#endif

	/* --- compile shaders --- */
	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, 1, &VS_PRESENT, NULL);
	glCompileShader(vs);

	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 1, &FS_PRESENT, NULL);
	glCompileShader(fs);

	g_prog_present = glCreateProgram();
	glAttachShader(g_prog_present, vs);
	glAttachShader(g_prog_present, fs);
	glLinkProgram(g_prog_present);
	glDeleteShader(vs);
	glDeleteShader(fs);

	/* --- create VAO + VBO --- */
	glGenVertexArrays(1, &g_vao_present);
	glBindVertexArray(g_vao_present);

	const float verts[] = {
		-1.f, -1.f,
		 1.f, -1.f,
		 1.f,  1.f,
		-1.f,  1.f
	};

	glGenBuffers(1, &g_vbo_present);
	glBindBuffer(GL_ARRAY_BUFFER, g_vbo_present);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);

	GLint loc = glGetUniformLocation(g_prog_present, "uTex");
	glUseProgram(g_prog_present);
	glUniform1i(loc, 0);
}

static GLFWcursor *g_cursors[LAM_CURSOR_COUNT];
static int g_cur_cursor = -1;

static void glfw_flush(void)
{
	int fbw = (int)lam_width, fbh = (int)lam_height;
	if (g_win)
		glfwGetFramebufferSize(g_win, &fbw, &fbh);
	if (fbw <= 0) fbw = (int)lam_width;
	if (fbh <= 0) fbh = (int)lam_height;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, fbw, fbh);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(g_prog_present);
	glBindVertexArray(g_vao_present);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g_tex);

	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	glfwSwapBuffers(g_win);
	glfwPollEvents();

	if (g_win) {
		int nfbw = 0, nfbh = 0;
		glfwGetFramebufferSize(g_win, &nfbw, &nfbh);
		if (nfbw > 0 && nfbh > 0 &&
		    ((uint32_t)nfbw != lam_width || (uint32_t)nfbh != lam_height)) {
			lam_fbo_resize((uint32_t)nfbw, (uint32_t)nfbh);
		}
	}
}

static void glfw_deinit(void)
{
	for (int i = 0; i < LAM_CURSOR_COUNT; i++) {
		if (g_cursors[i]) {
			glfwDestroyCursor(g_cursors[i]);
			g_cursors[i] = NULL;
		}
	}
	g_cur_cursor = -1;

	glDeleteProgram(g_prog_present);
	glDeleteBuffers(1, &g_vbo_present);
	glDeleteVertexArrays(1, &g_vao_present);

	glfwDestroyWindow(g_win);
	g_win = NULL;
	glfwTerminate();
}

static int glfw_running(void)
{
	return g_win ? !glfwWindowShouldClose(g_win) : 0;
}

static void glfw_set_title(const char *title)
{
	if (g_win && title)
		glfwSetWindowTitle(g_win, title);
}

static void glfw_resize(uint32_t w, uint32_t h)
{
	if (!g_win || w == 0 || h == 0)
		return;
	int cur_w = 0, cur_h = 0;
	glfwGetWindowSize(g_win, &cur_w, &cur_h);
	if (cur_w != (int)w || cur_h != (int)h) {
		glfwSetWindowSize(g_win, (int)w, (int)h);
		glfwPollEvents();
	}
	int fbw = 0, fbh = 0;
	glfwGetFramebufferSize(g_win, &fbw, &fbh);
	if (fbw > 0 && fbh > 0)
		lam_fbo_resize((uint32_t)fbw, (uint32_t)fbh);
}

static void glfw_set_cursor(int cursor)
{
	if (!g_win)
		return;
	if (cursor < 0 || cursor >= LAM_CURSOR_COUNT)
		cursor = LAM_CURSOR_DEFAULT;
	if (cursor == g_cur_cursor)
		return;
	g_cur_cursor = cursor;

	if (cursor == LAM_CURSOR_AUTO || cursor == LAM_CURSOR_DEFAULT) {
		glfwSetCursor(g_win, NULL);
		return;
	}

	if (!g_cursors[cursor]) {
		int shape = GLFW_ARROW_CURSOR;
		switch (cursor) {
		case LAM_CURSOR_POINTER:
		case LAM_CURSOR_GRAB:
		case LAM_CURSOR_GRABBING:
			shape = GLFW_HAND_CURSOR;
			break;
		case LAM_CURSOR_TEXT:
			shape = GLFW_IBEAM_CURSOR;
			break;
		case LAM_CURSOR_CROSSHAIR:
			shape = GLFW_CROSSHAIR_CURSOR;
			break;
		case LAM_CURSOR_NS_RESIZE:
			shape = GLFW_VRESIZE_CURSOR;
			break;
		case LAM_CURSOR_EW_RESIZE:
			shape = GLFW_HRESIZE_CURSOR;
			break;
		case LAM_CURSOR_NESW_RESIZE:
#ifdef GLFW_RESIZE_NESW_CURSOR
			shape = GLFW_RESIZE_NESW_CURSOR;
#else
			shape = GLFW_CROSSHAIR_CURSOR;
#endif
			break;
		case LAM_CURSOR_NWSE_RESIZE:
#ifdef GLFW_RESIZE_NWSE_CURSOR
			shape = GLFW_RESIZE_NWSE_CURSOR;
#else
			shape = GLFW_CROSSHAIR_CURSOR;
#endif
			break;
		case LAM_CURSOR_MOVE:
#ifdef GLFW_RESIZE_ALL_CURSOR
			shape = GLFW_RESIZE_ALL_CURSOR;
#else
			shape = GLFW_HAND_CURSOR;
#endif
			break;
		case LAM_CURSOR_NOT_ALLOWED:
#ifdef GLFW_NOT_ALLOWED_CURSOR
			shape = GLFW_NOT_ALLOWED_CURSOR;
#else
			shape = GLFW_ARROW_CURSOR;
#endif
			break;
		default:
			shape = GLFW_ARROW_CURSOR;
			break;
		}
		g_cursors[cursor] = glfwCreateStandardCursor(shape);
	}
	glfwSetCursor(g_win, g_cursors[cursor]);
}

static float glfw_content_scale(void)
{
	if (!g_win)
		return 1.0f;
	float xs = 1.0f, ys = 1.0f;
	glfwGetWindowContentScale(g_win, &xs, &ys);
	float s = (xs >= ys) ? xs : ys;
	if (!(s >= 1.0f && s <= 4.0f)) {
		int ww = 0, wh = 0, fbw = 0, fbh = 0;
		glfwGetWindowSize(g_win, &ww, &wh);
		glfwGetFramebufferSize(g_win, &fbw, &fbh);
		if (ww > 0 && fbw > 0)
			s = (float)fbw / (float)ww;
	}
	if (!(s >= 1.0f && s <= 4.0f))
		s = 1.0f;
	return s;
}

void qlfw_construct(void)
{
	lam_glfw.init          = glfw_init;
	lam_glfw.flush         = glfw_flush;
	lam_glfw.deinit        = glfw_deinit;
	lam_glfw.running       = glfw_running;
	lam_glfw.set_title     = glfw_set_title;
	lam_glfw.resize        = glfw_resize;
	lam_glfw.set_cursor    = glfw_set_cursor;
	lam_glfw.content_scale = glfw_content_scale;
}

