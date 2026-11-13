#include "../include/ttypt/lam.h"
#include "../include/ttypt/lam-cam.h"
#include "./gl.h"
#include "./be.h"
#include "./input.h"
#include <ttypt/stele.h>
#include <ttypt/corm.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>

lam_be_t lam_be;
lam_input_t lam_input;

GLuint g_fbo;
GLuint g_tex;

GLuint g_rbo_depth = 0;
GLuint g_prog_3d = 0;
GLint  g_uMVP_3d = -1;
GLint  g_uV0_3d = -1, g_uV1_3d = -1, g_uV2_3d = -1, g_uV3_3d = -1;
GLint  g_uUV_3d = -1, g_uTint_3d = -1, g_uSampler_3d = -1;

GLuint g_prog_tex;
static GLuint g_prog_fill;
GLint  g_uProj_tex, g_uDst_tex, g_uUV_tex,
       g_uTint_tex, g_uSampler;
static GLint  g_uProj_fill, g_uDst_fill, g_uColor_fill;

GLuint g_vao_dummy;

static uint32_t g_tex_map_hd;
uint32_t lam_height, lam_width;
screen_t screen;

float lam_ortho_M[16];

/* current render target (can be screen or offscreen cache) */
GLuint g_view_fbo = 0;
uint32_t g_view_w = 0;
uint32_t g_view_h = 0;

static const char *VS_TEX = "#version 330 core\n"
"uniform mat4 uProj;\n"
"uniform vec4 uDst;  // x,y,w,h em pixels\n"
"uniform vec4 uUV;   // u0,v0,u1,v1\n"
"out vec2 vUV;\n"
"void main(){\n"
"  // 0:(0,0) 1:(1,0) 2:(1,1) 3:(0,1)\n"
"  int id = gl_VertexID;\n"
"  vec2 p = vec2((id==1||id==2)?1.0:0.0, (id>=2)?1.0:0.0);\n"
"  vec2 pos = uDst.xy + p * uDst.zw;\n"
"  gl_Position = uProj * vec4(pos, 0.0, 1.0);\n"
"  vec2 uv0 = uUV.xy, uv1 = uUV.zw;\n"
"  vUV = mix(uv0, uv1, p);\n"
"}\n";

static const char *FS_TEX = "#version 330 core\n"
"in vec2 vUV;\n"
"uniform sampler2D uTex;\n"
"uniform vec4 uTint; // RGBA 0..1\n"
"out vec4 FragColor;\n"
"void main(){ FragColor = texture(uTex, vUV) * uTint; }\n";

static const char *VS_3D = "#version 330 core\n"
"uniform mat4 uMVP;\n"
"uniform vec3 uV0, uV1, uV2, uV3;\n"
"uniform vec4 uUV; // u0, v0, u1, v1\n"
"out vec2 vUV;\n"
"void main(){\n"
"  int id = gl_VertexID;\n"
"  vec3 pos = (id == 0) ? uV0 :\n"
"             (id == 1) ? uV1 :\n"
"             (id == 2) ? uV2 : uV3;\n"
"  vec2 p = vec2((id == 1 || id == 2) ? 1.0 : 0.0, (id >= 2) ? 1.0 : 0.0);\n"
"  vUV = mix(uUV.xy, uUV.zw, p);\n"
"  gl_Position = uMVP * vec4(pos, 1.0);\n"
"}\n";

static const char *FS_3D = "#version 330 core\n"
"in vec2 vUV;\n"
"uniform sampler2D uTex;\n"
"uniform vec4 uTint;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"  vec4 c = texture(uTex, vUV) * uTint;\n"
"  if (c.a < 0.1) discard;\n"
"  FragColor = c;\n"
"}\n";

/* shared vertex shader for fill/stroke (local coords relative to uDst.xy) */
const char *VS_FILL =
"#version 330 core\n"
"uniform mat4 uProj;\n"
"uniform vec4 uDst;     /* x,y,w,h */\n"
"out vec2 vPos;\n"
"void main(void)\n"
"{\n"
"	vec2 a;\n"
"	if      (gl_VertexID == 0) a = vec2(0.0, 0.0);\n"
"	else if (gl_VertexID == 1) a = vec2(1.0, 0.0);\n"
"	else if (gl_VertexID == 2) a = vec2(1.0, 1.0);\n"
"	else                       a = vec2(0.0, 1.0);\n"
"	vPos = a * uDst.zw;\n"
"	gl_Position = uProj * vec4(uDst.xy + vPos, 0.0, 1.0);\n"
"}\n";

static const char *FS_FILL =
"#version 330 core\n"
"uniform vec4 uColor;\n"
"out vec4 FragColor;\n"
"void main(void)\n"
"{\n"
"	FragColor = uColor;\n"
"}\n";

typedef struct {
	uint32_t id;
	uint32_t w, h;
} gl_tex_info_t;

GLuint lam_compile(GLenum type, const char *src) {
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	GLint ok;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		fprintf(stderr, "Shader compile error:\n%s\n", log);
		exit(1);
	}
	return s;
}

GLuint lam_link(GLuint vs, GLuint fs) {
	GLuint p = glCreateProgram();
	glAttachShader(p, vs);
	glAttachShader(p, fs);
	glLinkProgram(p);
	GLint ok;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetProgramInfoLog(p, sizeof(log), NULL, log);
		fprintf(stderr, "Program link error:\n%s\n", log);
		exit(1);
	}
	glDeleteShader(vs);
	glDeleteShader(fs);
	return p;
}

void lam_ortho(float w, float h, float *m) {
	m[0]=2.0f/w; m[4]=0;      m[8]=0;  m[12]=-1.0f;
	m[1]=0;      m[5]=-2.0f/h;m[9]=0;  m[13]= 1.0f;
	m[2]=0;      m[6]=0;      m[10]=1; m[14]= 0;
	m[3]=0;      m[7]=0;      m[11]=0; m[15]= 1.0f;
}

void shadow_init(void);

extern lam_be_t __attribute__((weak)) lam_fb;
extern lam_be_t lam_glfw;
extern lam_input_t __attribute__((weak)) lam_input_dev;
extern lam_input_t lam_input_glfw;

static void dummy_be_init(uint32_t *w, uint32_t *h) {
	if (w) *w = 1024;
	if (h) *h = 768;
}
static void dummy_be_flush(void) {}
static void dummy_be_deinit(void) {}
static lam_be_t lam_dummy_be = {
	.init = dummy_be_init,
	.flush = dummy_be_flush,
	.deinit = dummy_be_deinit
};
static void dummy_input_init(int flags) { (void)flags; }
static void dummy_input_deinit(void) {}
static void dummy_input_poll(void) {}
static lam_input_t lam_dummy_input = {
	.init = dummy_input_init,
	.deinit = dummy_input_deinit,
	.poll = dummy_input_poll
};

static void gl_init(uint32_t *w_r, uint32_t *h_r)
{
	uint32_t w, h;

	if (lam_be.init == dummy_be_init) {
		lam_be.init(w_r, h_r);
		lam_width = *w_r;
		lam_height = *h_r;
		screen.size = lam_width * lam_height * 4;
		screen.canvas = calloc(1, screen.size);
		screen.channels = 4;
		screen.min_x = 0; screen.min_y = 0;
		screen.max_x = lam_width; screen.max_y = lam_height;
		uint32_t cm_gl_tex = corm_reg(sizeof(gl_tex_info_t));
		g_tex_map_hd = corm_open(NULL, NULL, CM_U32, cm_gl_tex, 0xF, 0);
		return;
	}

#ifndef __APPLE__
	glewExperimental = GL_TRUE;
#endif
	lam_be.init(w_r, h_r);

	w = *w_r;
	h = *h_r;

#ifndef __APPLE__
	glewInit();
#endif

	// FBO
	glGenFramebuffers(1, &g_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);

	// texture of FBO
	glGenTextures(1, &g_tex);
	glBindTexture(GL_TEXTURE_2D, g_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// attach texture to FBO
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex, 0);

	// Depth renderbuffer
	glGenRenderbuffers(1, &g_rbo_depth);
	glBindRenderbuffer(GL_RENDERBUFFER, g_rbo_depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_rbo_depth);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "FBO Incomplete!\n");
		exit(1);
	}

	screen.size = w * h * 4;
	screen.canvas = malloc(screen.size);
	screen.channels = 4;
	screen.min_x = 0;
	screen.min_y = 0;
	screen.max_x = w;
	screen.max_y = h;

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glGenVertexArrays(1, &g_vao_dummy);

	// programs
	g_prog_tex  = lam_link(
			lam_compile(GL_VERTEX_SHADER, VS_TEX),
			lam_compile(GL_FRAGMENT_SHADER, FS_TEX));
	g_prog_fill = lam_link(
			lam_compile(GL_VERTEX_SHADER, VS_FILL),
			lam_compile(GL_FRAGMENT_SHADER, FS_FILL));
	g_prog_3d   = lam_link(
			lam_compile(GL_VERTEX_SHADER, VS_3D),
			lam_compile(GL_FRAGMENT_SHADER, FS_3D));

	// uniform locations - tex
	g_uProj_tex = glGetUniformLocation(g_prog_tex, "uProj");
	g_uDst_tex  = glGetUniformLocation(g_prog_tex, "uDst");
	g_uUV_tex   = glGetUniformLocation(g_prog_tex, "uUV");
	g_uTint_tex = glGetUniformLocation(g_prog_tex, "uTint");
	g_uSampler  = glGetUniformLocation(g_prog_tex, "uTex");

	// uniform locations - 3d
	g_uMVP_3d     = glGetUniformLocation(g_prog_3d, "uMVP");
	g_uV0_3d      = glGetUniformLocation(g_prog_3d, "uV0");
	g_uV1_3d      = glGetUniformLocation(g_prog_3d, "uV1");
	g_uV2_3d      = glGetUniformLocation(g_prog_3d, "uV2");
	g_uV3_3d      = glGetUniformLocation(g_prog_3d, "uV3");
	g_uUV_3d      = glGetUniformLocation(g_prog_3d, "uUV");
	g_uTint_3d    = glGetUniformLocation(g_prog_3d, "uTint");
	g_uSampler_3d = glGetUniformLocation(g_prog_3d, "uTex");
	glUseProgram(g_prog_3d);
	glUniform1i(g_uSampler_3d, 0);

	// uniform locations - fill
	g_uProj_fill  = glGetUniformLocation(g_prog_fill, "uProj");
	g_uDst_fill   = glGetUniformLocation(g_prog_fill, "uDst");
	g_uColor_fill = glGetUniformLocation(g_prog_fill, "uColor");

	glUseProgram(g_prog_tex);
	glUniform1i(g_uSampler, 0);

	lam_ortho((float)w, (float)h, lam_ortho_M);

	glUseProgram(g_prog_tex);
	glUniformMatrix4fv(g_uProj_tex,  1, GL_FALSE, lam_ortho_M);

	glUseProgram(g_prog_fill);
	glUniformMatrix4fv(g_uProj_fill, 1, GL_FALSE, lam_ortho_M);

	uint32_t cm_gl_tex = corm_reg(sizeof(gl_tex_info_t));
	g_tex_map_hd = corm_open(NULL, NULL, CM_U32, cm_gl_tex, 0xF, 0);

	shadow_init();

	glViewport(0, 0, (GLint)w, (GLint)h);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);

	lam_reset_viewport();
}

void img_load_all(void);
void qlfw_construct(void);
void __attribute__((weak)) fb_construct(void);
void input_glfw_construct(void);
void __attribute__((weak)) input_dev_construct(void);
void img_construct(void);
void png_construct(void);
void tile_construct(void);

void lam_ui_init(uint32_t screen_w, uint32_t screen_h);

__attribute__((constructor))
static void construct(void)
{
	img_construct();
	png_construct();
	tile_construct();
	img_load_all();
	lam_width = 1024;
	lam_height = 768;

#if defined(_WIN32) || defined(__APPLE__)
	qlfw_construct();
	input_glfw_construct();
	lam_be = lam_glfw;
	lam_input = lam_input_glfw;
#else
	if (getenv("DISPLAY") && *getenv("DISPLAY")) {
		qlfw_construct();
		input_glfw_construct();
		lam_be = lam_glfw;
		lam_input = lam_input_glfw;
	} else if (&lam_fb) {
		int test_fd = open("/dev/fb0", O_RDWR);
		if (test_fd >= 0) {
			close(test_fd);
			fb_construct();
			input_dev_construct();
			lam_be = lam_fb;
			lam_input = lam_input_dev;
		} else {
			lam_be = lam_dummy_be;
			lam_input = lam_dummy_input;
		}
	} else {
		lam_be = lam_dummy_be;
		lam_input = lam_dummy_input;
	}
#endif

}

void lam_init(void)
{
	gl_init(&lam_width, &lam_height);
	lam_input.init(0);
}

void lam_flush(void)
{
	if (lam_be.init == dummy_be_init)
		return;

	// update reading FBO -> screen.canvas
	glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, lam_width, lam_height, GL_BGRA, GL_UNSIGNED_BYTE, screen.canvas);

	// deliver to backend (that swaps buffers)
	lam_be.flush();

	// clean FBO
	glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
	glViewport(0, 0, (GLint)lam_width, (GLint)lam_height);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void lam_size(uint32_t *w, uint32_t *h)
{
	*w = lam_width;
	*h = lam_height;
}

static void gl_deinit(void)
{
	const void *key, *val;
	uint32_t it;

	if (g_rbo_depth) {
		glDeleteRenderbuffers(1, &g_rbo_depth);
		g_rbo_depth = 0;
	}
	if (g_prog_3d) {
		glDeleteProgram(g_prog_3d);
		g_prog_3d = 0;
	}

	glDeleteTextures(1, &g_tex);
	glDeleteFramebuffers(1, &g_fbo);

	it = corm_iter(g_tex_map_hd, NULL, 0);
	while (corm_next(&key, &val, it))
		glDeleteTextures(1, &((gl_tex_info_t *)val)->id);
	corm_close(g_tex_map_hd);
	free(screen.canvas);
	memset(&screen, 0, sizeof(screen));
}

void img_deinit(void);
void shadow_deinit(void);

void lam_deinit(void)
{
	lam_input.deinit();
	if (lam_be.init != dummy_be_init) {
		shadow_deinit();
		gl_deinit();
	}
	lam_be.deinit();
	img_deinit();
}

void lam_tex_draw_quad_3d(uint32_t ref,
                          const float v0[3], const float v1[3],
                          const float v2[3], const float v3[3],
                          uint32_t cx, uint32_t cy,
                          uint32_t sw, uint32_t sh,
                          uint32_t tint)
{
	if (lam_be.init == dummy_be_init)
		return;

	const gl_tex_info_t *tex = corm_get(g_tex_map_hd, &ref);
	if (!tex) return;

	float tex_u0 = (float)cx / (float)tex->w;
	float tex_v0 = (float)cy / (float)tex->h;
	float tex_u1 = (float)(cx + sw) / (float)tex->w;
	float tex_v1 = (float)(cy + sh) / (float)tex->h;
	float uv[4] = { tex_u0, tex_v0, tex_u1, tex_v1 };

	float ta = ((tint >> 24) & 0xFF) / 255.0f;
	float tr = ((tint >> 16) & 0xFF) / 255.0f;
	float tg = ((tint >>  8) & 0xFF) / 255.0f;
	float tb = ((tint      ) & 0xFF) / 255.0f;
	float rgba[4] = { tr, tg, tb, ta };

	float vp[16];
	lam_cam_get_vp(vp);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	glUseProgram(g_prog_3d);
	glBindVertexArray(g_vao_dummy);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex->id);

	glUniformMatrix4fv(g_uMVP_3d, 1, GL_FALSE, vp);
	glUniform3fv(g_uV0_3d, 1, v0);
	glUniform3fv(g_uV1_3d, 1, v1);
	glUniform3fv(g_uV2_3d, 1, v2);
	glUniform3fv(g_uV3_3d, 1, v3);
	glUniform4fv(g_uUV_3d, 1, uv);
	glUniform4fv(g_uTint_3d, 1, rgba);

	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

void lam_tex_draw_x(uint32_t ref, int32_t x, int32_t y,
                    uint32_t cx, uint32_t cy, uint32_t sw, uint32_t sh,
                    uint32_t dw, uint32_t dh, uint32_t tint)
{
	if (lam_be.init == dummy_be_init)
		return;

	glDisable(GL_DEPTH_TEST);

	const gl_tex_info_t *tex = corm_get(g_tex_map_hd, &ref);
	if (!tex) return;

	float u0 = (float)cx / (float)tex->w;
	float v0 = (float)cy / (float)tex->h;
	float u1 = (float)(cx + sw) / (float)tex->w;
	float v1 = (float)(cy + sh) / (float)tex->h;

	float dst[4] = { (float)x, (float)y, (float)dw, (float)dh };
	float uv [4] = { u0, v0, u1, v1 };

	float ta = ((tint >> 24) & 0xFF) / 255.0f;
	float tr = ((tint >> 16) & 0xFF) / 255.0f;
	float tg = ((tint >>  8) & 0xFF) / 255.0f;
	float tb = ((tint      ) & 0xFF) / 255.0f;
	float rgba[4] = { tr, tg, tb, ta };

	glUseProgram(g_prog_tex);
	glBindVertexArray(g_vao_dummy);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex->id);

	glUniform4fv(g_uDst_tex, 1, dst);
	glUniform4fv(g_uUV_tex,  1, uv);
	glUniform4fv(g_uTint_tex,1, rgba);
	lam_apply_ortho(g_uProj_tex);

	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

void lam_tex_reg(uint32_t ref, uint8_t *data, uint32_t w, uint32_t h)
{
	gl_tex_info_t tex = { .w = w, .h = h, .id = 0 };

	if (lam_be.init != dummy_be_init) {
		glGenTextures(1, &tex.id);
		glBindTexture(GL_TEXTURE_2D, tex.id);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
			     GL_RGBA, GL_UNSIGNED_BYTE, data);
	}

	corm_put(g_tex_map_hd, &ref, &tex);
}

void lam_tex_upd(uint32_t ref, uint32_t x, uint32_t y,
		 uint32_t w, uint32_t h, uint8_t *data)
{
	if (lam_be.init == dummy_be_init)
		return;

	const gl_tex_info_t *t = corm_get(g_tex_map_hd, &ref);
	if (!t)
		return;

	glBindTexture(GL_TEXTURE_2D, t->id);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h,
			GL_RGBA, GL_UNSIGNED_BYTE, data);
}

void lam_tex_ureg(uint32_t ref)
{
	const gl_tex_info_t *t = corm_get(g_tex_map_hd, &ref);

	if (t) {
		glDeleteTextures(1, &t->id);
		corm_del(g_tex_map_hd, &ref);
	}
}

void lam_poll(void)
{
	lam_input.poll();
}

void lam_fill(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color)
{
	if (lam_be.init == dummy_be_init)
		return;

	float a = ((color >> 24) & 0xFF) / 255.0f;
	float r = ((color >> 16) & 0xFF) / 255.0f;
	float g = ((color >>  8) & 0xFF) / 255.0f;
	float b = ((color      ) & 0xFF) / 255.0f;

	float dst[4]  = { (float)x, (float)y, (float)w, (float)h };
	float rgba[4] = { r, g, b, a };

	glUseProgram(g_prog_fill);
	glBindVertexArray(g_vao_dummy);
	glUniform4fv(g_uDst_fill, 1, dst);
	glUniform4fv(g_uColor_fill, 1, rgba);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

void lam_clear(uint32_t color)
{
	if (lam_be.init == dummy_be_init)
		return;

	float a = ((color >> 24) & 0xFF) / 255.0f;
	float r = ((color >> 16) & 0xFF) / 255.0f;
	float g = ((color >>  8) & 0xFF) / 255.0f;
	float b = ((color      ) & 0xFF) / 255.0f;

	glBindFramebuffer(GL_FRAMEBUFFER, g_view_fbo);
	glViewport(0, 0, (GLint)g_view_w, (GLint)g_view_h);
	glClearColor(r, g, b, a);
	glClear(GL_COLOR_BUFFER_BIT);
}

void lam_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t border_color, uint32_t border_width)
{
	if (!w || !h || !border_width)
		return;

	if (border_width * 2 >= w || border_width * 2 >= h) {
		lam_fill(x, y, w, h, border_color);
		return;
	}

	/* Top border */
	lam_fill(x, y, w, border_width, border_color);
	/* Bottom border */
	lam_fill(x, y + (int32_t)h - (int32_t)border_width, w, border_width, border_color);
	/* Left border */
	lam_fill(x, y + (int32_t)border_width, border_width, h - 2 * border_width, border_color);
	/* Right border */
	lam_fill(x + (int32_t)w - (int32_t)border_width, y + (int32_t)border_width, border_width, h - 2 * border_width, border_color);
}

void lam_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color, uint32_t thickness)
{
	if (thickness == 0)
		thickness = 1;

	/* Fast path: horizontal line */
	if (y0 == y1) {
		int32_t min_x = (x0 < x1) ? x0 : x1;
		uint32_t w = (uint32_t)((x0 > x1) ? (x0 - x1 + 1) : (x1 - x0 + 1));
		int32_t y = y0 - (int32_t)(thickness / 2);
		lam_fill(min_x, y, w, thickness, color);
		return;
	}

	/* Fast path: vertical line */
	if (x0 == x1) {
		int32_t min_y = (y0 < y1) ? y0 : y1;
		uint32_t h = (uint32_t)((y0 > y1) ? (y0 - y1 + 1) : (y1 - y0 + 1));
		int32_t x = x0 - (int32_t)(thickness / 2);
		lam_fill(x, min_y, thickness, h, color);
		return;
	}

	/* Diagonal line: rasterize along vector */
	float dx = (float)(x1 - x0);
	float dy = (float)(y1 - y0);
	float len = sqrtf(dx * dx + dy * dy);
	if (len < 1e-4f) {
		lam_fill(x0 - (int32_t)(thickness / 2), y0 - (int32_t)(thickness / 2), thickness, thickness, color);
		return;
	}

	int32_t steps = (int32_t)(len + 0.5f);
	float half_t = (float)thickness * 0.5f;
	for (int32_t i = 0; i <= steps; i++) {
		float t = (float)i / (float)steps;
		int32_t px = (int32_t)((float)x0 + dx * t - half_t);
		int32_t py = (int32_t)((float)y0 + dy * t - half_t);
		lam_fill(px, py, thickness, thickness, color);
	}
}

void lam_set_viewport(GLuint fbo, uint32_t w, uint32_t h)
{
	g_view_fbo = fbo;
	g_view_w = w;
	g_view_h = h;

	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, (GLint)w, (GLint)h);
}

void lam_reset_viewport(void)
{
	g_view_fbo = g_fbo;
	g_view_w = lam_width;
	g_view_h = lam_height;

	glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
	glViewport(0, 0, (GLint)lam_width, (GLint)lam_height);
}

void lam_apply_ortho(GLint uProj)
{
    float M[16];
    /* usa o viewport em vigor, não a matriz global do arranque */
    lam_ortho((float)g_view_w, (float)g_view_h, M);
    glUniformMatrix4fv(uProj, 1, GL_FALSE, M);
}

GLuint lam_current_fbo(void) {
    GLint fbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    return (GLuint)fbo;
}
