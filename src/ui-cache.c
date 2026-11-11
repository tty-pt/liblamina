#include "./gl.h"
#include "./ui.h"
#include <string.h>
#include <stdlib.h>
#include <xxhash.h>
#include <math.h>

extern GLuint g_fbo, g_vao_dummy, g_prog_tex;
extern GLint g_uDst_tex, g_uUV_tex, g_uTint_tex;

/* Each div optionally owns a cache */
typedef struct {
	GLuint		tex;
	uint32_t	w, h;		/* tamanho do TEX do cache (já com margens) */
	int		dirty;
	uint64_t	hash;

	/* margem extra para sombra (px) */
	int32_t		pad_l, pad_t, pad_r, pad_b;
} lam_cache_t;

/* Attach to lam_ui_div via its style->cache or extended struct in your engine */
#define DIV_CACHE(d)	((lam_cache_t *)((d)->_cache))

void lam_cache_init(void)
{
	/* nothing global for now */
}

void lam_cache_deinit(void)
{
	/* nothing */
}

uint64_t lam_cache_hash_style(const lam_ui_style_t *s, uint32_t w, uint32_t h)
{
	XXH64_state_t *state = XXH64_createState();
	XXH64_reset(state, 0);
	XXH64_update(state, s, sizeof(*s));
	XXH64_update(state, &w, sizeof(w));
	XXH64_update(state, &h, sizeof(h));
	uint64_t h64 = XXH64_digest(state);
	XXH64_freeState(state);
	return h64;
}

void lam_shadow_margins(const lam_ui_style_t *s,
			int32_t *pl, int32_t *pt,
			int32_t *pr, int32_t *pb)
{
	if (!s || !s->box_shadow_color || s->box_shadow_blur <= 0.f) {
		*pl = *pt = *pr = *pb = 0;
		return;
	}

	const float K  = 3.0f;		/* blur tail coverage (~99.7%) */
	const float r  = s->box_shadow_blur * K;

	*pl = (int32_t)(r + fmaxf(0.f, -s->box_shadow_offset_x));
	*pr = (int32_t)(r + fmaxf(0.f,  s->box_shadow_offset_x));
	*pt = (int32_t)(r + fmaxf(0.f, -s->box_shadow_offset_y));
	*pb = (int32_t)(r + fmaxf(0.f,  s->box_shadow_offset_y));
}

void lam_cache_invalidate(lam_ui_div_t *d)
{
	if (!d || !DIV_CACHE(d))
		return;

	DIV_CACHE(d)->dirty = 1;
}

void lam_cache_invalidate_tree(lam_ui_div_t *root)
{
	if (!root)
		return;

	lam_cache_invalidate(root);
	for (lam_ui_div_t *c = root->first_child; c; c = c->next_sibling)
		lam_cache_invalidate_tree(c);
}

int lam_cache_valid(const lam_ui_div_t *d)
{
	lam_cache_t *c = DIV_CACHE(d);
	int32_t pl, pt, pr, pb;
	uint32_t want_w, want_h;

	if (!c)
		return 0;
	if (c->dirty || !c->tex)
		return 0;

	lam_shadow_margins(d->style, &pl, &pt, &pr, &pb);
	want_w = (uint32_t)(d->w + pl + pr);
	want_h = (uint32_t)(d->h + pt + pb);

	if (c->w != want_w || c->h != want_h)
		return 0;

	return 1;
}

static void translate_subtree(lam_ui_div_t *d, int32_t ox, int32_t oy)
{
	lam_ui_div_t *c;

	if (!d)
		return;

	d->x += ox;
	d->y += oy;

	for (c = d->first_child; c; c = c->next_sibling)
		translate_subtree(c, ox, oy);
}

/* build the cache (offscreen render of subtree) */
void lam_cache_build(lam_ui_div_t *d)
{
    lam_cache_t *c = DIV_CACHE(d);
    int old_dirty;
    GLuint tex, fbo;
    GLint prev_fbo, vp[4];
    int32_t pl, pt, pr, pb;
    uint64_t hash;
    int32_t ox, oy;

    if (!c)
        d->_cache = c = calloc(1, sizeof(*c));

    if (d->w == LAM_UI_AUTO || d->h == LAM_UI_AUTO) {
        c->dirty = 1;
        return;
    }

    /* compute margins required for shadow */
    lam_shadow_margins(d->style, &pl, &pt, &pr, &pb);

    hash = lam_cache_hash_style(d->style, d->w, d->h);

    if (c->tex)
        glDeleteTextures(1, &c->tex);

    c->pad_l = pl;
    c->pad_t = pt;
    c->pad_r = pr;
    c->pad_b = pb;

    c->w = (uint32_t)(d->w + pl + pr);
    c->h = (uint32_t)(d->h + pt + pb);
    c->hash = hash;

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLint)c->w, (GLint)c->h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glGenFramebuffers(1, &fbo);

    /* save current framebuffer and viewport */
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_VIEWPORT, vp);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, tex, 0);
    glViewport(0, 0, (GLint)c->w, (GLint)c->h);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    /*
     * Translate the div subtree so that (d->x, d->y) maps to (pl, pt)
     * inside the FBO. This is the *only* translation needed,
     * as (pl, pt) already account for shadow offsets.
     */
    ox = -((int32_t)d->x) + pl;
    oy = -((int32_t)d->y) + pt;

    old_dirty = c->dirty;
    c->dirty = 1;

    translate_subtree(d, ox, oy);
    render_div_raw(d);
    translate_subtree(d, -ox, -oy);

    c->dirty = old_dirty;

    /* restore previous framebuffer and viewport */
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glDeleteFramebuffers(1, &fbo);

    c->tex = tex;
    c->dirty = 0;
}

/* draw cached image */
void lam_cache_draw(const lam_ui_div_t *d, int32_t x, int32_t y)
{
	const lam_cache_t *c = DIV_CACHE(d);
	float dst[4], uv[4], tint[4] = { 1, 1, 1, 1 };

	if (!c || !c->tex)
		return;

	/* only draw inside div area */
	dst[0] = (float)x;
	dst[1] = (float)y;
	dst[2] = (float)d->w;
	dst[3] = (float)d->h;

	/* UV rect to sample only the central portion (excluding shadow margins) */
	uv[0] = (float)c->pad_l / (float)c->w;
	uv[1] = (float)c->pad_t / (float)c->h;
	uv[2] = (float)(c->pad_l + d->w) / (float)c->w;
	uv[3] = (float)(c->pad_t + d->h) / (float)c->h;

	glUseProgram(g_prog_tex);
	glBindVertexArray(g_vao_dummy);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, c->tex);
	glUniform4fv(g_uDst_tex, 1, dst);
	glUniform4fv(g_uUV_tex, 1, uv);
	glUniform4fv(g_uTint_tex, 1, tint);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

