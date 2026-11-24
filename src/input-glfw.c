#include "input.h"
#include "be.h"
#include <GLFW/glfw3.h>
#include <math.h>

lam_input_t lam_input_glfw;

extern GLFWwindow *g_win;
static int g_grab = 0;

static void key_cb(GLFWwindow *w, int key, int scancode, int action, int mods) {
	(void)w; (void)scancode;
	if (key < 0)
		return;

	lam_event_t ev = {0};
	ev.type   = LAM_EV_KEY;
	ev.code   = (uint32_t)key;
	ev.action = (action == GLFW_RELEASE) ? 0 : (action == GLFW_REPEAT ? 2 : 1);
	ev.mods   = mods;
	lam_mouse_pos(&ev.x, &ev.y);
	input_emit_event(&ev);

	if (action == GLFW_PRESS)
		input_call((unsigned short)key, 1, 1);
	else if (action == GLFW_RELEASE)
		input_call((unsigned short)key, 0, 1);
	else if (action == GLFW_REPEAT)
		input_call((unsigned short)key, 1, 1);
}

static void char_cb(GLFWwindow *w, unsigned int codepoint) {
	(void)w;
	lam_event_t ev = {0};
	ev.type   = LAM_EV_CHAR;
	ev.code   = (uint32_t)codepoint;
	ev.action = 1;
	lam_mouse_pos(&ev.x, &ev.y);
	input_emit_event(&ev);
}

static void scale_cursor_pos(GLFWwindow *w, double x, double y,
                             int32_t *ox, int32_t *oy)
{
	double s = (double)lam_get_dpi_scale();
	if (s < 1.0) s = 1.0;
	int ww = 0, wh = 0;
	if (w)
		glfwGetWindowSize(w, &ww, &wh);
	double px = x, py = y;
	if (ww > 0 && wh > 0 && lam_width > 0 && lam_height > 0 &&
	    ((uint32_t)ww != lam_width || (uint32_t)wh != lam_height)) {
		px = x * (double)lam_width / (double)ww;
		py = y * (double)lam_height / (double)wh;
	}
	*ox = (int32_t)lround(px / s);
	*oy = (int32_t)lround(py / s);
}

static void sync_cursor_pos(GLFWwindow *w, int32_t *ox, int32_t *oy)
{
	if (w) {
		double cx = 0.0, cy = 0.0;
		glfwGetCursorPos(w, &cx, &cy);
		scale_cursor_pos(w, cx, cy, ox, oy);
		input_set_mouse_pos(*ox, *oy);
	} else {
		lam_mouse_pos(ox, oy);
	}
}

static void mouse_cb(GLFWwindow *w, int button, int action, int mods) {
	lam_event_t ev = {0};
	ev.type   = LAM_EV_MOUSE_BTN;
	ev.code   = (uint32_t)button;
	ev.action = (action == GLFW_PRESS) ? 1 : 0;
	ev.mods   = mods;
	sync_cursor_pos(w, &ev.x, &ev.y);
	input_emit_event(&ev);

	input_call((unsigned short)button,
			(action == GLFW_PRESS) ? 1 : 0,
			2);
}

static void cursor_cb(GLFWwindow *w, double x, double y) {
	int32_t ix = 0, iy = 0;
	scale_cursor_pos(w, x, y, &ix, &iy);
	input_set_mouse_pos(ix, iy);

	lam_event_t ev = {0};
	ev.type = LAM_EV_MOUSE_MOVE;
	ev.x    = ix;
	ev.y    = iy;
	input_emit_event(&ev);

	input_call(0, 0, 3);
}

static void scroll_cb(GLFWwindow *w, double xoffset, double yoffset) {
	lam_event_t ev = {0};
	ev.type = LAM_EV_SCROLL;
	ev.dx   = (float)xoffset;
	ev.dy   = (float)yoffset;
	sync_cursor_pos(w, &ev.x, &ev.y);
	input_emit_event(&ev);
}

static void fbsize_cb(GLFWwindow *w, int width, int height) {
	(void)w;
	if (width > 0 && height > 0)
		lam_fbo_resize((uint32_t)width, (uint32_t)height);

	double s = (double)lam_get_dpi_scale();
	if (s < 1.0) s = 1.0;

	lam_event_t ev = {0};
	ev.type = LAM_EV_RESIZE;
	ev.x    = (int32_t)lround((double)width / s);
	ev.y    = (int32_t)lround((double)height / s);
	input_emit_event(&ev);

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
	glfwSetCharCallback(g_win, char_cb);
	glfwSetMouseButtonCallback(g_win, mouse_cb);
	glfwSetCursorPosCallback(g_win, cursor_cb);
	glfwSetScrollCallback(g_win, scroll_cb);
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
