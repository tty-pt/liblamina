/*
 * This file was initially based off on Pedro's work.
 * Since, it has been changed a lot, but credit is still due.
 */

#include "../include/ttypt/lam.h"
#include "./gl.h"
#include "./be.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <ttypt/stele.h>

#define LOAD_GL(name) do { \
	name = (typeof(name)) eglGetProcAddress(#name); \
	if (!name) fprintf(stderr, "missing GL func %s\n", #name); \
} while (0)

static int fb_fd = -1;
static size_t fb_size_bytes;
static uint8_t *fb_mem;

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLContext egl_ctx = EGL_NO_CONTEXT;
static EGLSurface egl_surf = EGL_NO_SURFACE;

lam_be_t lam_fb;

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, __u32)
#endif

static struct fb_fix_screeninfo g_finfo;
static struct fb_var_screeninfo g_vinfo;

static int g_stride;
static int g_bpp;
static int g_use_pan;
static int g_front;
static int g_back = 1;
static int g_page_lines;
static size_t g_page_bytes;

static int wait_vsync(void)
{
	uint32_t dummy = 0;
	if (ioctl(fb_fd, FBIO_WAITFORVSYNC, &dummy) == 0)
		return 0;
	return -1;
}

static EGLConfig egl_choose_config(void)
{
	const EGLint cfg_attrs[] = {
		EGL_SURFACE_TYPE,     EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE,  EGL_OPENGL_BIT,
		EGL_RED_SIZE,         8,
		EGL_GREEN_SIZE,       8,
		EGL_BLUE_SIZE,        8,
		EGL_ALPHA_SIZE,       8,
		EGL_NONE
	};
	EGLConfig cfg;
	EGLint n = 0;

	CBUG(!eglChooseConfig(egl_dpy, cfg_attrs, &cfg, 1, &n) || n < 1, "eglChooseConfig");
	return cfg;
}

static void egl_make_current_or_pbuffer(EGLConfig cfg, EGLint w, EGLint h)
{
	if (eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_ctx))
		return;

	const EGLint pb_attribs[] = { EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE };
	egl_surf = eglCreatePbufferSurface(egl_dpy, cfg, pb_attribs);
	CBUG(egl_surf == EGL_NO_SURFACE, "eglCreatePbufferSurface");

	CBUG(!eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx), "eglMakeCurrent");
}

static void egl_headless_init(uint32_t w, uint32_t h)
{
	EGLint major = 0, minor = 0;
	PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display_ext;

	get_platform_display_ext =
		(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");

	if (get_platform_display_ext)
		egl_dpy = get_platform_display_ext(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);

	if (egl_dpy == EGL_NO_DISPLAY)
		egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);

	CBUG(!eglInitialize(egl_dpy, &major, &minor), "eglInitialize");
	CBUG(!eglBindAPI(EGL_OPENGL_API), "eglBindAPI");

	EGLConfig cfg = egl_choose_config();
	egl_ctx = eglCreateContext(egl_dpy, cfg, EGL_NO_CONTEXT, NULL);
	CBUG(egl_ctx == EGL_NO_CONTEXT, "eglCreateContext");

	egl_make_current_or_pbuffer(cfg, (EGLint)w, (EGLint)h);
	fprintf(stderr, "EGL: contexto inicializado (OpenGL headless)\n");

}

static void try_enable_double_buffering(void)
{
	struct fb_var_screeninfo req = g_vinfo;
	req.yres_virtual = g_vinfo.yres * 2;

	if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &req) != 0)
		return;
	if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &g_vinfo) != 0)
		return;
	if (g_vinfo.yres_virtual >= g_vinfo.yres * 2)
		g_use_pan = 1;
}

static inline void copy_to_fb_page(uint8_t *dst_page, const uint8_t *src,
		int w, int h, int bpp, int stride)
{
	size_t row_bytes = (size_t)w * bpp;
	for (int y = 0; y < h; y++)
		memcpy(dst_page + (size_t)y * stride,
				src + (size_t)y * row_bytes, row_bytes);
}

void fb_init(uint32_t *w, uint32_t *h)
{
	fb_fd = open("/dev/fb0", O_RDWR);
	if (fb_fd < 0) {
		fprintf(stderr, "liblamina: open(/dev/fb0) failed\n");
		if (w) *w = 1024;
		if (h) *h = 768;
		return;
	}

	if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &g_finfo) == -1 ||
	    ioctl(fb_fd, FBIOGET_VSCREENINFO, &g_vinfo) == -1) {
		fprintf(stderr, "liblamina: FBIOGET_VSCREENINFO failed\n");
		close(fb_fd);
		fb_fd = -1;
		if (w) *w = 1024;
		if (h) *h = 768;
		return;
	}

	*w  = g_vinfo.xres;
	*h = g_vinfo.yres;
	g_bpp     = g_vinfo.bits_per_pixel / 8;
	g_stride  = g_finfo.line_length;

	try_enable_double_buffering();

	g_page_lines = g_vinfo.yres;
	g_page_bytes = (size_t)g_stride * g_page_lines;
	fb_size_bytes = (size_t)g_finfo.smem_len;

	fb_mem = mmap(NULL, fb_size_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (fb_mem == MAP_FAILED) {
		fprintf(stderr, "liblamina: mmap(/dev/fb0) failed\n");
		close(fb_fd);
		fb_fd = -1;
		fb_mem = NULL;
		return;
	}

	WARN("FB: %ux%u, %u bpp, stride=%d, yvirt=%u, pan=%s\n",
			*w, *h, g_vinfo.bits_per_pixel, g_stride,
			g_vinfo.yres_virtual, g_use_pan ? "on" : "off");

	egl_headless_init(*w, *h);

	LOAD_GL(glGenFramebuffers);
	LOAD_GL(glBindFramebuffer);
	LOAD_GL(glFramebufferTexture2D);
	LOAD_GL(glCheckFramebufferStatus);
	LOAD_GL(glDeleteFramebuffers);
	LOAD_GL(glActiveTexture);

	LOAD_GL(glGenVertexArrays);
	LOAD_GL(glBindVertexArray);
	LOAD_GL(glCreateShader);
	LOAD_GL(glShaderSource);
	LOAD_GL(glCompileShader);
	LOAD_GL(glGetShaderiv);
	LOAD_GL(glGetShaderInfoLog);
	LOAD_GL(glCreateProgram);
	LOAD_GL(glAttachShader);
	LOAD_GL(glLinkProgram);
	LOAD_GL(glGetProgramiv);
	LOAD_GL(glGetProgramInfoLog);
	LOAD_GL(glDeleteShader);
	LOAD_GL(glUseProgram);
	LOAD_GL(glGetUniformLocation);
	LOAD_GL(glUniform1f);
	LOAD_GL(glUniform2f);
	LOAD_GL(glUniform1i);
	LOAD_GL(glUniform4fv);
	LOAD_GL(glUniformMatrix4fv);
	LOAD_GL(glDetachShader);
	LOAD_GL(glBlendFuncSeparate);
}

void fb_flush(void)
{
	const uint8_t *src = (const uint8_t *)screen.canvas;

	if (g_use_pan) {
		size_t yoff_lines = g_back ? g_page_lines : 0;
		uint8_t *dst = fb_mem + (size_t)yoff_lines * g_stride;
		struct fb_var_screeninfo v = g_vinfo;
		int tmp;

		copy_to_fb_page(dst, src, lam_width, lam_height, g_bpp, g_stride);
		(void)wait_vsync();

		v.yoffset = g_back ? g_page_lines : 0;
		if (ioctl(fb_fd, FBIOPAN_DISPLAY, &v) == -1)
			goto single_path;

		g_vinfo = v;
		tmp = g_front;
		g_front = g_back;
		g_back = tmp;
		return;
	}

single_path:
	(void)wait_vsync();

	if ((int)((size_t)lam_width * g_bpp) == g_stride) {
		ssize_t ret = pwrite(fb_fd, src, (size_t)lam_height * g_stride, 0);
		CBUG(ret < 0, "pwrite(fb)");
		return;
	}
	copy_to_fb_page(fb_mem, src, lam_width, lam_height, g_bpp, g_stride);
}

void fb_deinit(void)
{
	if (egl_dpy != EGL_NO_DISPLAY) {
		CBUG(!eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT), "eglMakeCurrent");

		if (egl_surf != EGL_NO_SURFACE)
			eglDestroySurface(egl_dpy, egl_surf);
		if (egl_ctx != EGL_NO_CONTEXT)
			eglDestroyContext(egl_dpy, egl_ctx);

		eglTerminate(egl_dpy);
	}

	if (fb_mem && fb_mem != MAP_FAILED)
		munmap(fb_mem, fb_size_bytes);

	if (fb_fd >= 0)
		close(fb_fd);

	free(screen.canvas);
	memset(&screen, 0, sizeof(screen));
}

void
fb_construct(void)
{
	lam_fb.init = fb_init;
	lam_fb.deinit = fb_deinit;
	lam_fb.flush = fb_flush;
}
