#include "./gl.h"

#include <ttypt/corm.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

struct key_round_color {
	uint32_t	w, h;
	float		tl, tr, br, bl;
	float		width;
	uint32_t	fill_rgba;
	uint32_t	stroke_rgba;
};

static uint32_t g_round_tex_map_hd;

extern GLuint g_fbo, g_vao_dummy, g_prog_tex;
extern GLint g_uDst_tex, g_uUV_tex, g_uTint_tex;
extern float lam_ortho_M[16];

static const char *FS_FILL_ROUND =
"#version 330 core\n"
"in vec2 vPos;\n"
"uniform vec4 uColor;\n"
"uniform vec4 uDst;\n"
"uniform vec4 uRadius;\n"
"out vec4 FragColor;\n"
"float sdRoundedBox(vec2 p, vec2 b, vec4 r)\n"
"{\n"
"\tfloat m = 0.5 * min(b.x, b.y);\n"
"\tr = clamp(r, 0.0, m);\n"
"\tvec2 pc = p - b * 0.5;\n"
"\tvec2 bh = b * 0.5;\n"
"\tfloat rq = (pc.x > 0.0) ? ((pc.y > 0.0) ? r.y : r.z)\n"
"\t\t\t       : ((pc.y > 0.0) ? r.x : r.w);\n"
"\tvec2 q = abs(pc) - bh + rq;\n"
"\treturn min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - rq;\n"
"}\n"
"void main(void)\n"
"{\n"
"\tvec2 size = uDst.zw;\n"
"\tfloat d = sdRoundedBox(vPos, size, uRadius);\n"
"\tfloat aa = fwidth(d);\n"
"\tfloat edge_in = smoothstep(0.0, aa, -d);\n"
"\tif (edge_in <= 0.0) discard;\n"
"\tFragColor = vec4(uColor.rgb, uColor.a * edge_in);\n"
"}\n";

static const char *FS_STROKE_ROUND =
"#version 330 core\n"
"in vec2 vPos;\n"
"uniform vec4 uColor;\n"
"uniform vec4 uDst;\n"
"uniform vec4 uRadius;\n"
"uniform float uWidth;\n"
"out vec4 FragColor;\n"
"float sdRoundedBox(vec2 p, vec2 b, vec4 r)\n"
"{\n"
"\tfloat m = 0.5 * min(b.x, b.y);\n"
"\tr = clamp(r, 0.0, m);\n"
"\tvec2 pc = p - b * 0.5;\n"
"\tvec2 bh = b * 0.5;\n"
"\tfloat rq = (pc.x > 0.0) ? ((pc.y > 0.0) ? r.y : r.z)\n"
"\t\t\t       : ((pc.y > 0.0) ? r.x : r.w);\n"
"\tvec2 q = abs(pc) - bh + rq;\n"
"\treturn min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - rq;\n"
"}\n"
"void main(void)\n"
"{\n"
"\tvec2 size = uDst.zw;\n"
"\tfloat w = uWidth * 1.25;\n"
"\tfloat dOuter = sdRoundedBox(vPos, size, uRadius);\n"
"\tfloat dInner = sdRoundedBox(vPos - vec2(w, w), size - 2.0 * vec2(w), max(uRadius - w, 0.0));\n"
"\tif (dOuter > 0.0 || dInner < 0.0) discard;\n"
"\tfloat aa = fwidth(dOuter);\n"
"\tfloat aOuter = smoothstep(0.0, aa, -dOuter);\n"
"\tfloat aInner = smoothstep(0.0, aa, dInner);\n"
"\tfloat alpha = aOuter * aInner;\n"
"\tFragColor = vec4(uColor.rgb, uColor.a * alpha);\n"
"}\n";

static const char *VS_SHADOW =
"#version 330 core\n"
"uniform mat4 uProj;\n"
"uniform vec4 uShadowQuad;\n"
"uniform vec4 uDivGeo;\n"
"out vec2 vPos;\n"
"void main(void)\n"
"{\n"
"\tvec2 a;\n"
"\tif (gl_VertexID == 0) a = vec2(0.0, 0.0);\n"
"\telse if (gl_VertexID == 1) a = vec2(1.0, 0.0);\n"
"\telse if (gl_VertexID == 2) a = vec2(1.0, 1.0);\n"
"\telse a = vec2(0.0, 1.0);\n"
"\tvec2 worldPos = uShadowQuad.xy + a * uShadowQuad.zw;\n"
"\tgl_Position = uProj * vec4(worldPos, 0.0, 1.0);\n"
"\tvPos = worldPos - uDivGeo.xy;\n"
"}\n";

static const char *FS_SHADOW_ROUND =
"#version 330 core\n"
"in vec2 vPos;\n"
"uniform vec4 uColor;\n"
"uniform vec4 uDivGeo;\n"
"uniform vec4 uRadius;\n"
"uniform float uSpread;\n"
"uniform vec2 uOffset;\n"
"uniform float uClipDiv;\n"
"out vec4 FragColor;\n"
"float sdRoundedBox(vec2 p, vec2 b, vec4 r)\n"
"{\n"
"\tfloat m = 0.5 * min(b.x, b.y);\n"
"\tr = clamp(r, 0.0, m);\n"
"\tvec2 pc = p - b * 0.5;\n"
"\tvec2 bh = b * 0.5;\n"
"\tfloat rq = (pc.x > 0.0) ? ((pc.y > 0.0) ? r.y : r.z)\n"
"\t\t\t       : ((pc.y > 0.0) ? r.x : r.w);\n"
"\tvec2 q = abs(pc) - bh + rq;\n"
"\treturn min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - rq;\n"
"}\n"
"void main(void)\n"
"{\n"
"\tvec2 size = uDivGeo.zw;\n"
"\tvec2 adj_off = 0.5 * uOffset;\n"
"\tvec2 adj_size = max(size - abs(uOffset) + 2.0, vec2(1.0));\n"
"\tfloat d0 = sdRoundedBox(vPos, size, uRadius);\n"
"\tfloat d1 = sdRoundedBox(vPos - adj_off, adj_size, uRadius);\n"
"\tfloat aa = max(fwidth(d1), 1e-4);\n"
"\tif (d1 <= -aa) discard;\n"
"\tfloat edge = smoothstep(-aa, aa, d1);\n"
"\tfloat sig = max(uSpread, 0.5);\n"
"\tfloat fall = exp(-(d1 * d1) / (sig * sig));\n"
"\tfloat alpha = uColor.a * edge * fall;\n"
"\tfloat clip = smoothstep(0.0, aa, d0);\n"
"\talpha *= mix(1.0, clip, clamp(uClipDiv, 0.0, 1.0));\n"
"\tif (alpha < 0.001) discard;\n"
"\tFragColor = vec4(uColor.rgb, alpha);\n"
"}\n";

static GLuint prog_shadow_round;
static GLint uProj_shadow, uDivGeo_shadow, uColor_shadow, uRadius_shadow;
static GLint uSpread_shadow, uOffset_shadow, uClipDiv_shadow;
static GLint uShadowQuad_shadow;

static GLuint prog_fill_round, prog_stroke_round;
static GLint uProj_fill, uDst_fill, uColor_fill, uRadius_fill;
static GLint uProj_stroke, uDst_stroke, uColor_stroke, uRadius_stroke, uWidth_stroke;

static inline void
rgba_u32_to_f4(uint32_t c, float out[4])
{
	out[3] = ((c >> 24) & 0xff) / 255.0f;
	out[0] = ((c >> 16) & 0xff) / 255.0f;
	out[1] = ((c >> 8) & 0xff) / 255.0f;
	out[2] = ((c >> 0) & 0xff) / 255.0f;
}

void
lam_border_radius(uint32_t bg_color, uint32_t border_color,
		  int32_t x, int32_t y, uint32_t w, uint32_t h,
		  float tl, float tr, float br, float bl,
		  float border_width)
{
	float dst[4], radius[4], color[4];

	dst[0] = (float)x;
	dst[1] = (float)y;
	dst[2] = (float)w;
	dst[3] = (float)h;

	radius[0] = tl;
	radius[1] = tr;
	radius[2] = br;
	radius[3] = bl;

	glBindVertexArray(g_vao_dummy);

	if (bg_color & 0xff000000u) {
		rgba_u32_to_f4(bg_color, color);

		glUseProgram(prog_fill_round);
		glUniformMatrix4fv(uProj_fill, 1, GL_FALSE, lam_ortho_M);
		glUniform4fv(uDst_fill, 1, dst);
		glUniform4fv(uRadius_fill, 1, radius);
		glUniform4fv(uColor_fill, 1, color);

		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	}

	if (border_width > 0.0f && (border_color & 0xff000000u)) {
		rgba_u32_to_f4(border_color, color);

		glUseProgram(prog_stroke_round);
		glUniformMatrix4fv(uProj_stroke, 1, GL_FALSE, lam_ortho_M);
		glUniform4fv(uDst_stroke, 1, dst);
		glUniform4fv(uRadius_stroke, 1, radius);
		glUniform1f(uWidth_stroke, border_width);
		glUniform4fv(uColor_stroke, 1, color);

		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	}
}

void
lam_box_shadow(uint32_t color,
	       int32_t x, int32_t y, uint32_t w, uint32_t h,
	       float tl, float tr, float br, float bl,
	       float blur_radius, float offset_x, float offset_y)
{
	float div_geo[4], shadow_quad[4], radius[4], col[4];
	const float K = 3.0f;
	const float r = blur_radius * K;
	const float off_l = fmaxf(0.f, -offset_x);
	const float off_r = fmaxf(0.f,  offset_x);
	const float off_t = fmaxf(0.f, -offset_y);
	const float off_b = fmaxf(0.f,  offset_y);
	const float bias = 0.5f;

	rgba_u32_to_f4(color, col);

	div_geo[0] = (float)x + 1.0f;
	div_geo[1] = (float)y + 1.0f;
	div_geo[2] = (float)w - 2.0f;
	div_geo[3] = (float)h - 2.0f;

	shadow_quad[0] = (float)x - (r + off_l) + bias;
	shadow_quad[1] = (float)y - (r + off_t) + bias;
	shadow_quad[2] = (float)w + (r * 2.0f + off_l + off_r) - bias * 2.0f;
	shadow_quad[3] = (float)h + (r * 2.0f + off_t + off_b) - bias * 2.0f;

	radius[0] = tl;
	radius[1] = tr;
	radius[2] = br;
	radius[3] = bl;

	glUseProgram(prog_shadow_round);
	glUniformMatrix4fv(uProj_shadow, 1, GL_FALSE, lam_ortho_M);
	glUniform4fv(uDivGeo_shadow, 1, div_geo);
	glUniform4fv(uShadowQuad_shadow, 1, shadow_quad);
	glUniform4fv(uColor_shadow, 1, col);
	glUniform4fv(uRadius_shadow, 1, radius);
	glUniform1f(uSpread_shadow, blur_radius);
	glUniform2f(uOffset_shadow, offset_x, offset_y);
	glUniform1f(uClipDiv_shadow, 1.0f);

	glBindVertexArray(g_vao_dummy);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

void
shadow_init(void)
{
	uint32_t cm_gl_tex;

	cm_gl_tex = corm_reg(sizeof(GLuint));
	g_round_tex_map_hd = corm_open(NULL, NULL, CM_HNDL, cm_gl_tex, 0xf, 0);

	prog_fill_round = lam_link(
		lam_compile(GL_VERTEX_SHADER, VS_FILL),
		lam_compile(GL_FRAGMENT_SHADER, FS_FILL_ROUND));
	uProj_fill = glGetUniformLocation(prog_fill_round, "uProj");
	uDst_fill = glGetUniformLocation(prog_fill_round, "uDst");
	uColor_fill = glGetUniformLocation(prog_fill_round, "uColor");
	uRadius_fill = glGetUniformLocation(prog_fill_round, "uRadius");

	prog_stroke_round = lam_link(
		lam_compile(GL_VERTEX_SHADER, VS_FILL),
		lam_compile(GL_FRAGMENT_SHADER, FS_STROKE_ROUND));
	uProj_stroke = glGetUniformLocation(prog_stroke_round, "uProj");
	uDst_stroke = glGetUniformLocation(prog_stroke_round, "uDst");
	uColor_stroke = glGetUniformLocation(prog_stroke_round, "uColor");
	uRadius_stroke = glGetUniformLocation(prog_stroke_round, "uRadius");
	uWidth_stroke = glGetUniformLocation(prog_stroke_round, "uWidth");

	prog_shadow_round = lam_link(
		lam_compile(GL_VERTEX_SHADER, VS_SHADOW),
		lam_compile(GL_FRAGMENT_SHADER, FS_SHADOW_ROUND));
	uProj_shadow = glGetUniformLocation(prog_shadow_round, "uProj");
	uDivGeo_shadow = glGetUniformLocation(prog_shadow_round, "uDivGeo");
	uShadowQuad_shadow = glGetUniformLocation(prog_shadow_round, "uShadowQuad");
	uColor_shadow = glGetUniformLocation(prog_shadow_round, "uColor");
	uRadius_shadow = glGetUniformLocation(prog_shadow_round, "uRadius");
	uSpread_shadow = glGetUniformLocation(prog_shadow_round, "uSpread");
	uOffset_shadow = glGetUniformLocation(prog_shadow_round, "uOffset");
	uClipDiv_shadow = glGetUniformLocation(prog_shadow_round, "uClipDiv");
}

void
shadow_deinit(void)
{
	const void *key, *val;
	uint32_t it;

	if (g_round_tex_map_hd) {
		it = corm_iter(g_round_tex_map_hd, NULL, 0);
		while (corm_next(&key, &val, it))
			glDeleteTextures(1, (const GLuint *)val);
		corm_close(g_round_tex_map_hd);
		g_round_tex_map_hd = 0;
	}

	if (prog_fill_round) {
		glDeleteProgram(prog_fill_round);
		prog_fill_round = 0;
	}
	if (prog_stroke_round) {
		glDeleteProgram(prog_stroke_round);
		prog_stroke_round = 0;
	}
}
