#include "input.h"
#include <GLFW/glfw3.h>

lam_input_t lam_input_glfw;

extern GLFWwindow *g_win;
static int g_grab = 0;

static void key_cb(GLFWwindow *w, int key, int scancode, int action, int mods) {
	(void)w; (void)scancode; (void)mods;
	if (key < 0)
		return;

	if (action == GLFW_PRESS)
		input_call((unsigned short)key, 1, 1);
	else if (action == GLFW_RELEASE)
		input_call((unsigned short)key, 0, 1);
	else if (action == GLFW_REPEAT)
		input_call((unsigned short)key, 1, 1);
}

static void mouse_cb(GLFWwindow *w, int button, int action, int mods) {
	(void)w; (void)mods;
	input_call((unsigned short)button,
			(action == GLFW_PRESS) ? 1 : 0,
			2);
}

static void cursor_cb(GLFWwindow *w, double x, double y) {
	(void)w; (void)x; (void)y;
	input_call(0, 0, 3);
}

static void fbsize_cb(GLFWwindow *w, int width, int height) {
	(void)w; (void)width; (void)height;
	input_call(0, 0, 4);
}

static void input_init(int grab)
{
	g_grab = grab;
	if (g_grab)
		glfwSetInputMode(g_win, GLFW_CURSOR,
				GLFW_CURSOR_DISABLED);

	/* Register callbacks */
	glfwSetKeyCallback(g_win, key_cb);
	glfwSetMouseButtonCallback(g_win, mouse_cb);
	glfwSetCursorPosCallback(g_win, cursor_cb);
	glfwSetFramebufferSizeCallback(g_win, fbsize_cb);
}

static void input_poll(void)
{
	glfwPollEvents();
}

static void input_deinit(void)
{
}

void
input_glfw_construct(void)
{
	lam_input_glfw.init = input_init;
	lam_input_glfw.deinit = input_deinit;
	lam_input_glfw.poll = input_poll;
}
