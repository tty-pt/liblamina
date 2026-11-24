#include "./gl.h"
#include "../include/ttypt/lam-ui.h"

#include <ttypt/corm.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

extern uint32_t lam_ui_screen_w;
extern uint32_t lam_ui_screen_h;

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
"uniform vec4 uColor2;\n"
"uniform int uGradType;\n"
"uniform float uGradAngle;\n"
"uniform vec4 uDst;\n"
"uniform vec4 uRadius;\n"
"out vec4 FragColor;\n"
"float sdRoundedBox(vec2 p, vec2 b, vec4 r)\n"
"{\n"
"\tfloat m = 0.5 * min(b.x, b.y);\n"
"\tr = clamp(r, 0.0, m);\n"
"\tvec2 pc = p - b * 0.5;\n"
"\tvec2 bh = b * 0.5;\n"
"\tfloat rq = (pc.x > 0.0) ? ((pc.y > 0.0) ? r.z : r.y)\n"
"\t\t\t       : ((pc.y > 0.0) ? r.w : r.x);\n"
"\tvec2 q = abs(pc) - bh + rq;\n"
"\treturn min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - rq;\n"
"}\n"
"void main(void)\n"
"{\n"
"\tvec2 size = uDst.zw;\n"
"\tfloat d = sdRoundedBox(vPos, size, uRadius);\n"
"\tfloat aa = max(fwidth(d), 0.5);\n"
"\tfloat edge_in = smoothstep(0.0, aa, -d);\n"
"\tif (edge_in <= 0.0) discard;\n"
"\tvec4 col = uColor;\n"
"\tif (uGradType == 1) {\n"
"\t\tfloat rad = radians(uGradAngle);\n"
"\t\tvec2 dir = vec2(sin(rad), -cos(rad));\n"
"\t\tvec2 rel = vPos - 0.5 * size;\n"
"\t\tfloat span = max(abs(dir.x) * size.x + abs(dir.y) * size.y, 1.0);\n"
"\t\tfloat t = clamp(dot(rel, dir) / span + 0.5, 0.0, 1.0);\n"
"\t\tcol = mix(uColor, uColor2, t);\n"
"\t} else if (uGradType == 2) {\n"
"\t\tvec2 uv = vPos / max(size, vec2(1.0));\n"
"\t\tfloat t = clamp(length((uv - vec2(0.4, 0.4)) * 1.45), 0.0, 1.0);\n"
"\t\tcol = mix(uColor, uColor2, t);\n"
"\t}\n"
"\tFragColor = vec4(col.rgb, col.a * edge_in);\n"
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
"\tfloat rq = (pc.x > 0.0) ? ((pc.y > 0.0) ? r.z : r.y)\n"
"\t\t\t       : ((pc.y > 0.0) ? r.w : r.x);\n"
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
"\tfloat aa = max(fwidth(dOuter), 0.5);\n"
"\tfloat aOuter = smoothstep(0.0, aa, -dOuter);\n"
"\tfloat aInner = smoothstep(0.0, aa, dInner);\n"
"\tfloat alpha = aOuter * aInner;\n"
"\tFragColor = vec4(uColor.rgb, uColor.a * alpha);\n"
"}\n";

static const char *FS_TEX_ROUND =
"#version 330 core\n"
"in vec2 vPos;\n"
"uniform vec4 uDst;\n"
"uniform vec4 uRadius;\n"
"uniform vec4 uUV;\n"
"uniform vec4 uBlendCol;\n"
"uniform int uBlendMode;\n"
"uniform float uOpacity;\n"
"uniform sampler2D uTex;\n"
"out vec4 FragColor;\n"
"float sdRoundedBox(vec2 p, vec2 b, vec4 r)\n"
"{\n"
"\tfloat m = 0.5 * min(b.x, b.y);\n"
"\tr = clamp(r, 0.0, m);\n"
"\tvec2 pc = p - b * 0.5;\n"
"\tvec2 bh = b * 0.5;\n"
"\tfloat rq = (pc.x > 0.0) ? ((pc.y > 0.0) ? r.z : r.y)\n"
"\t\t\t       : ((pc.y > 0.0) ? r.w : r.x);\n"
"\tvec2 q = abs(pc) - bh + rq;\n"
"\treturn min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - rq;\n"
"}\n"
"void main(void)\n"
"{\n"
"\tvec2 size = uDst.zw;\n"
"\tfloat d = sdRoundedBox(vPos, size, uRadius);\n"
"\tfloat aa = max(fwidth(d), 0.5);\n"
"\tfloat edge_in = smoothstep(0.0, aa, -d);\n"
"\tif (edge_in <= 0.0) discard;\n"
"\tvec2 uv = uUV.xy + (vPos / max(size, vec2(1.0))) * uUV.zw;\n"
"\tvec4 tex = texture(uTex, uv);\n"
"\tvec3 rgb = tex.rgb;\n"
"\tif (uBlendMode == 1) {\n"
"\t\trgb = mix(rgb, rgb * uBlendCol.rgb, uBlendCol.a);\n"
"\t} else if (uBlendMode == 2) {\n"
"\t\trgb = mix(rgb, vec3(1.0) - (vec3(1.0) - rgb) * (vec3(1.0) - uBlendCol.rgb), uBlendCol.a);\n"
"\t}\n"
"\tFragColor = vec4(rgb, tex.a * uOpacity * edge_in);\n"
"}\n";

static const char *FS_BACKDROP_BLUR =
"#version 330 core\n"
"in vec2 vPos;\n"
"uniform vec4 uDst;\n"
"uniform vec4 uRadius;\n"
"uniform vec4 uCopyRect;\n" /* cx, cy, cw, ch in UI screen px */
"uniform vec2 uTexSize;\n"  /* allocated scratch texture w, h */
"uniform float uBlur;\n"
"uniform sampler2D uTex;\n"
"out vec4 FragColor;\n"
"float sdRoundedBox(vec2 p, vec2 b, vec4 r)\n"
"{\n"
"\tfloat m = 0.5 * min(b.x, b.y);\n"
"\tr = clamp(r, 0.0, m);\n"
"\tvec2 pc = p - b * 0.5;\n"
"\tvec2 bh = b * 0.5;\n"
"\tfloat rq = (pc.x > 0.0) ? ((pc.y > 0.0) ? r.z : r.y)\n"
"\t\t\t       : ((pc.y > 0.0) ? r.w : r.x);\n"
"\tvec2 q = abs(pc) - bh + rq;\n"
"\treturn min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - rq;\n"
"}\n"
"void main(void)\n"
"{\n"
"\tvec2 size = uDst.zw;\n"
"\tfloat d = sdRoundedBox(vPos, size, uRadius);\n"
"\tfloat aa = max(fwidth(d), 0.5);\n"
"\tfloat edge_in = smoothstep(0.0, aa, -d);\n"
"\tif (edge_in <= 0.0) discard;\n"
"\tvec2 sp = uDst.xy + vPos;\n"
"\tvec2 base = vec2(sp.x - uCopyRect.x, (uCopyRect.y + uCopyRect.w) - sp.y);\n"
"\tvec2 uv_min = vec2(0.5) / uTexSize;\n"
"\tvec2 uv_max = (uCopyRect.zw - vec2(0.5)) / uTexSize;\n"
"\tvec2 uv0 = clamp(base / uTexSize, uv_min, uv_max);\n"
"\tvec2 step_uv = vec2(uBlur) / uTexSize;\n"
"\tvec3 acc = texture(uTex, uv0).rgb * 0.20;\n"
"\tacc += texture(uTex, clamp(uv0 + vec2( step_uv.x,  0.0), uv_min, uv_max)).rgb * 0.10;\n"
"\tacc += texture(uTex, clamp(uv0 + vec2(-step_uv.x,  0.0), uv_min, uv_max)).rgb * 0.10;\n"
"\tacc += texture(uTex, clamp(uv0 + vec2( 0.0,  step_uv.y), uv_min, uv_max)).rgb * 0.10;\n"
"\tacc += texture(uTex, clamp(uv0 + vec2( 0.0, -step_uv.y), uv_min, uv_max)).rgb * 0.10;\n"
"\tacc += texture(uTex, clamp(uv0 + vec2( step_uv.x,  step_uv.y) * 0.707, uv_min, uv_max)).rgb * 0.06;\n"
"\tacc += texture(uTex, clamp(uv0 + vec2(-step_uv.x,  step_uv.y) * 0.707, uv_min, uv_max)).rgb * 0.06;\n"
"\tacc += texture(uTex, clamp(uv0 + vec2( step_uv.x, -step_uv.y) * 0.707, uv_min, uv_max)).rgb * 0.06;\n"
"\tacc += texture(uTex, clamp(uv0 + vec2(-step_uv.x, -step_uv.y) * 0.707, uv_min, uv_max)).rgb * 0.06;\n"
"\tvec2 step2 = step_uv * 1.75;\n"
"\tacc += texture(uTex, clamp(uv0 + vec2( step2.x,  0.0), uv_min, uv_max)).rgb * 0.04;\n"
"\tacc += texture(uTex, clamp(uv0 + vec2(-step2.x,  0.0), uv_min, uv_max)).rgb * 0.04;\n"
"\tacc += texture(uTex, clamp(uv0 + vec2( 0.0,  step2.y), uv_min, uv_max)).rgb * 0.04;\n"
"\tacc += texture(uTex, clamp(uv0 + vec2( 0.0, -step2.y), uv_min, uv_max)).rgb * 0.04;\n"
"\tFragColor = vec4(acc, edge_in);\n"
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
"\tfloat rq = (pc.x > 0.0) ? ((pc.y > 0.0) ? r.z : r.y)\n"
"\t\t\t       : ((pc.y > 0.0) ? r.w : r.x);\n"
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
static GLint uProj_fill, uDst_fill, uColor_fill, uColor2_fill, uGradType_fill, uGradAngle_fill, uRadius_fill;
static GLint uProj_stroke, uDst_stroke, uColor_stroke, uRadius_stroke, uWidth_stroke;

static GLuint prog_tex_round;
static GLint uProj_texr, uDst_texr, uRadius_texr, uUV_texr, uBlendCol_texr, uBlendMode_texr, uOpacity_texr, uTex_texr;

static GLuint prog_backdrop_blur;
static GLint uProj_blur, uDst_blur, uRadius_blur, uCopyRect_blur, uTexSize_blur, uBlur_blur, uTex_blur;
static GLuint g_backdrop_tex = 0;
static uint32_t g_backdrop_w = 0, g_backdrop_h = 0;

static inline void
rgba_u32_to_f4(uint32_t c, float out[4])
{
	out[3] = ((c >> 24) & 0xff) / 255.0f;
	out[0] = ((c >> 16) & 0xff) / 255.0f;
	out[1] = ((c >> 8) & 0xff) / 255.0f;
	out[2] = ((c >> 0) & 0xff) / 255.0f;
}

void
lam_border_radius_grad(uint32_t bg_color, uint32_t bg_color2,
		       uint8_t grad_type, float grad_angle_deg,
		       uint32_t border_color,
		       int32_t x, int32_t y, uint32_t w, uint32_t h,
		       float tl, float tr, float br, float bl,
		       float border_width)
{
	float dst[4], radius[4], color[4], color2[4];

	if (!lam_has_gl() || w == 0 || h == 0)
		return;

	dst[0] = (float)x;
	dst[1] = (float)y;
	dst[2] = (float)w;
	dst[3] = (float)h;

	radius[0] = tl;
	radius[1] = tr;
	radius[2] = br;
	radius[3] = bl;

	glBindVertexArray(g_vao_dummy);

	if ((bg_color & 0xff000000u) || (grad_type != 0 && (bg_color2 & 0xff000000u))) {
		rgba_u32_to_f4(bg_color, color);
		rgba_u32_to_f4(grad_type ? bg_color2 : bg_color, color2);

		glUseProgram(prog_fill_round);
		lam_apply_ortho(uProj_fill);
		glUniform4fv(uDst_fill, 1, dst);
		glUniform4fv(uRadius_fill, 1, radius);
		glUniform4fv(uColor_fill, 1, color);
		glUniform4fv(uColor2_fill, 1, color2);
		glUniform1i(uGradType_fill, (GLint)grad_type);
		glUniform1f(uGradAngle_fill, grad_angle_deg);

		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	}

	if (border_width > 0.0f && (border_color & 0xff000000u)) {
		rgba_u32_to_f4(border_color, color);

		glUseProgram(prog_stroke_round);
		lam_apply_ortho(uProj_stroke);
		glUniform4fv(uDst_stroke, 1, dst);
		glUniform4fv(uRadius_stroke, 1, radius);
		glUniform1f(uWidth_stroke, border_width);
		glUniform4fv(uColor_stroke, 1, color);

		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	}
}

void
lam_border_radius(uint32_t bg_color, uint32_t border_color,
		  int32_t x, int32_t y, uint32_t w, uint32_t h,
		  float tl, float tr, float br, float bl,
		  float border_width)
{
	lam_border_radius_grad(bg_color, 0, 0, 180.0f, border_color,
			       x, y, w, h, tl, tr, br, bl, border_width);
}

void
lam_tex_draw_box(uint32_t tex_ref,
		 int32_t x, int32_t y, uint32_t w, uint32_t h,
		 float tl, float tr, float br, float bl,
		 uint8_t fit_mode, uint8_t blend_mode,
		 uint32_t bg_tint, float opacity)
{
	GLuint tex_id = 0;
	uint32_t tw = 0, th = 0;

	if (!lam_has_gl() || !prog_tex_round || w == 0 || h == 0)
		return;
	if (!lam_gl_tex_lookup(tex_ref, &tex_id, &tw, &th) || !tex_id || !tw || !th)
		return;

	float dst[4] = { (float)x, (float)y, (float)w, (float)h };
	float uv[4]  = { 0.0f, 0.0f, 1.0f, 1.0f };

	if (fit_mode == 1) {
		/* cover: fill dest, center-crop texture aspect */
		float src_ar = (float)tw / (float)th;
		float dst_ar = (float)w / (float)h;
		if (src_ar > dst_ar) {
			float keep = dst_ar / src_ar;
			uv[0] = (1.0f - keep) * 0.5f;
			uv[2] = keep;
		} else if (src_ar < dst_ar) {
			float keep = src_ar / dst_ar;
			uv[1] = (1.0f - keep) * 0.5f;
			uv[3] = keep;
		}
	} else if (fit_mode == 2) {
		/* contain: preserve aspect inside dest box */
		float sx = (float)w / (float)tw;
		float sy = (float)h / (float)th;
		float s  = (sx < sy) ? sx : sy;
		float dw = (float)tw * s;
		float dh = (float)th * s;
		dst[0] = (float)x + ((float)w - dw) * 0.5f;
		dst[1] = (float)y + ((float)h - dh) * 0.5f;
		dst[2] = dw;
		dst[3] = dh;
	}

	float radius[4] = { tl, tr, br, bl };
	float blend_col[4];
	rgba_u32_to_f4(bg_tint, blend_col);

	glUseProgram(prog_tex_round);
	lam_apply_ortho(uProj_texr);
	glUniform4fv(uDst_texr, 1, dst);
	glUniform4fv(uRadius_texr, 1, radius);
	glUniform4fv(uUV_texr, 1, uv);
	glUniform4fv(uBlendCol_texr, 1, blend_col);
	glUniform1i(uBlendMode_texr, (GLint)blend_mode);
	glUniform1f(uOpacity_texr, opacity);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex_id);
	glUniform1i(uTex_texr, 0);

	glBindVertexArray(g_vao_dummy);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

void
lam_backdrop_blur_round(int32_t x, int32_t y, uint32_t w, uint32_t h,
			float tl, float tr, float br, float bl,
			float blur_px)
{
	if (!lam_has_gl() || !prog_backdrop_blur || w == 0 || h == 0 || blur_px <= 0.0f)
		return;

	int32_t sw = (int32_t)g_view_w;
	int32_t sh = (int32_t)g_view_h;
	if (sw <= 0 || sh <= 0)
		return;

	float s = (g_view_fbo == g_fbo) ? lam_get_dpi_scale() : 1.0f;
	if (s < 1.0f) s = 1.0f;

	int32_t px0 = (int32_t)floorf((float)x * s);
	int32_t py0 = (int32_t)floorf((float)y * s);
	int32_t px1 = (int32_t)ceilf((float)(x + (int32_t)w) * s);
	int32_t py1 = (int32_t)ceilf((float)(y + (int32_t)h) * s);
	int32_t pad = (int32_t)ceilf(blur_px * s * 2.0f) + 2;
	int32_t cx0 = px0 - pad;
	int32_t cy0 = py0 - pad;
	int32_t cx1 = px1 + pad;
	int32_t cy1 = py1 + pad;
	if (cx0 < 0) cx0 = 0;
	if (cy0 < 0) cy0 = 0;
	if (cx1 > sw) cx1 = sw;
	if (cy1 > sh) cy1 = sh;
	int32_t cw = cx1 - cx0;
	int32_t ch = cy1 - cy0;
	if (cw <= 0 || ch <= 0)
		return;

	if (!g_backdrop_tex) {
		glGenTextures(1, &g_backdrop_tex);
		glBindTexture(GL_TEXTURE_2D, g_backdrop_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	} else {
		glBindTexture(GL_TEXTURE_2D, g_backdrop_tex);
	}

	if ((uint32_t)cw > g_backdrop_w || (uint32_t)ch > g_backdrop_h) {
		if ((uint32_t)cw > g_backdrop_w) g_backdrop_w = (uint32_t)cw;
		if ((uint32_t)ch > g_backdrop_h) g_backdrop_h = (uint32_t)ch;
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
			     (GLsizei)g_backdrop_w, (GLsizei)g_backdrop_h,
			     0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	}

	int32_t gl_y = sh - (cy0 + ch);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g_backdrop_tex);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cx0, gl_y, cw, ch);

	float dst[4]    = { (float)x, (float)y, (float)w, (float)h };
	float radius[4] = { tl, tr, br, bl };
	float copyr[4]  = { (float)cx0 / s, (float)cy0 / s, (float)cw / s, (float)ch / s };

	glUseProgram(prog_backdrop_blur);
	lam_apply_ortho(uProj_blur);
	glUniform4fv(uDst_blur, 1, dst);
	glUniform4fv(uRadius_blur, 1, radius);
	glUniform4fv(uCopyRect_blur, 1, copyr);
	glUniform2f(uTexSize_blur, (float)g_backdrop_w / s, (float)g_backdrop_h / s);
	glUniform1f(uBlur_blur, blur_px);
	glUniform1i(uTex_blur, 0);

	glBindVertexArray(g_vao_dummy);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
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
	lam_apply_ortho(uProj_shadow);
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
	uColor2_fill = glGetUniformLocation(prog_fill_round, "uColor2");
	uGradType_fill = glGetUniformLocation(prog_fill_round, "uGradType");
	uGradAngle_fill = glGetUniformLocation(prog_fill_round, "uGradAngle");
	uRadius_fill = glGetUniformLocation(prog_fill_round, "uRadius");

	prog_stroke_round = lam_link(
		lam_compile(GL_VERTEX_SHADER, VS_FILL),
		lam_compile(GL_FRAGMENT_SHADER, FS_STROKE_ROUND));
	uProj_stroke = glGetUniformLocation(prog_stroke_round, "uProj");
	uDst_stroke = glGetUniformLocation(prog_stroke_round, "uDst");
	uColor_stroke = glGetUniformLocation(prog_stroke_round, "uColor");
	uRadius_stroke = glGetUniformLocation(prog_stroke_round, "uRadius");
	uWidth_stroke = glGetUniformLocation(prog_stroke_round, "uWidth");

	prog_tex_round = lam_link(
		lam_compile(GL_VERTEX_SHADER, VS_FILL),
		lam_compile(GL_FRAGMENT_SHADER, FS_TEX_ROUND));
	uProj_texr = glGetUniformLocation(prog_tex_round, "uProj");
	uDst_texr = glGetUniformLocation(prog_tex_round, "uDst");
	uRadius_texr = glGetUniformLocation(prog_tex_round, "uRadius");
	uUV_texr = glGetUniformLocation(prog_tex_round, "uUV");
	uBlendCol_texr = glGetUniformLocation(prog_tex_round, "uBlendCol");
	uBlendMode_texr = glGetUniformLocation(prog_tex_round, "uBlendMode");
	uOpacity_texr = glGetUniformLocation(prog_tex_round, "uOpacity");
	uTex_texr = glGetUniformLocation(prog_tex_round, "uTex");

	prog_backdrop_blur = lam_link(
		lam_compile(GL_VERTEX_SHADER, VS_FILL),
		lam_compile(GL_FRAGMENT_SHADER, FS_BACKDROP_BLUR));
	uProj_blur = glGetUniformLocation(prog_backdrop_blur, "uProj");
	uDst_blur = glGetUniformLocation(prog_backdrop_blur, "uDst");
	uRadius_blur = glGetUniformLocation(prog_backdrop_blur, "uRadius");
	uCopyRect_blur = glGetUniformLocation(prog_backdrop_blur, "uCopyRect");
	uTexSize_blur = glGetUniformLocation(prog_backdrop_blur, "uTexSize");
	uBlur_blur = glGetUniformLocation(prog_backdrop_blur, "uBlur");
	uTex_blur = glGetUniformLocation(prog_backdrop_blur, "uTex");

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

	if (g_backdrop_tex) {
		glDeleteTextures(1, &g_backdrop_tex);
		g_backdrop_tex = 0;
		g_backdrop_w = g_backdrop_h = 0;
	}

	if (prog_fill_round) {
		glDeleteProgram(prog_fill_round);
		prog_fill_round = 0;
	}
	if (prog_stroke_round) {
		glDeleteProgram(prog_stroke_round);
		prog_stroke_round = 0;
	}
	if (prog_tex_round) {
		glDeleteProgram(prog_tex_round);
		prog_tex_round = 0;
	}
	if (prog_backdrop_blur) {
		glDeleteProgram(prog_backdrop_blur);
		prog_backdrop_blur = 0;
	}
	if (prog_shadow_round) {
		glDeleteProgram(prog_shadow_round);
		prog_shadow_round = 0;
	}
}
