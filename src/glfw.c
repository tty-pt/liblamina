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

static void glfw_flush(void)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, lam_width, lam_height);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(g_prog_present);
	glBindVertexArray(g_vao_present);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g_tex);

	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	glfwSwapBuffers(g_win);
	glfwPollEvents();
}

static void glfw_deinit(void)
{
	glDeleteProgram(g_prog_present);
	glDeleteBuffers(1, &g_vbo_present);
	glDeleteVertexArrays(1, &g_vao_present);

	glfwDestroyWindow(g_win);
	glfwTerminate();
}

void qlfw_construct(void)
{
	lam_glfw.init   = glfw_init;
	lam_glfw.flush  = glfw_flush;
	lam_glfw.deinit = glfw_deinit;
}

