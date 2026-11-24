#include "./ui.h"
#include "./ui-cache.h"
#include "./gl.h"
#include "../include/ttypt/lam-font.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include <ttypt/corm.h>

#define MAX_CHILDREN 512

typedef struct lam_ui_container {
	lam_ui_div_t *node;
	int32_t inner_x, inner_y;
	int32_t inner_w, inner_h;
	float inner_main, inner_cross;
} lam_ui_container_t;

typedef struct {
	int32_t x, y, w, h;
	int     active;
} clip_rect_t;

uint32_t lam_ui_screen_w = 1024;
uint32_t lam_ui_screen_h = 768;

void style_init(void);

static char *xstrdup(const char *s)
{
	if (!s) return NULL;
	size_t n = strlen(s);
	char *r = malloc(n + 1);
	if (r) memcpy(r, s, n + 1);
	return r;
}

static inline float clampf(float v, float lo, float hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

static inline uint32_t mul_alpha(uint32_t col, float opacity)
{
	if (opacity >= 0.999f || col == 0) return col;
	uint32_t a = (col >> 24) & 0xFFu;
	uint32_t na = (uint32_t)(a * clampf(opacity, 0.0f, 1.0f) + 0.5f);
	if (na > 255) na = 255;
	return (na << 24) | (col & 0x00FFFFFFu);
}

void lam_ui_init(uint32_t screen_w, uint32_t screen_h)
{
	lam_ui_screen_w = screen_w ? screen_w : 1024;
	lam_ui_screen_h = screen_h ? screen_h : 768;
	style_init();
}

void lam_ui_resize(uint32_t screen_w, uint32_t screen_h)
{
	if (screen_w) lam_ui_screen_w = screen_w;
	if (screen_h) lam_ui_screen_h = screen_h;
}

/* ── Length resolution helpers ──────────────────────────────────────────── */

static inline float style_em_px(const lam_ui_style_t *s)
{
	return (s && s->font_size_is_px && s->font_size > 0)
		? (float)s->font_size : 16.0f;
}

static int32_t resolve_len(lam_len_t l, int32_t parent_px, float em_px, int *is_set)
{
	if (l.unit == LAM_LEN_AUTO) {
		if (is_set) *is_set = 0;
		return LAM_UI_AUTO;
	}
	if (is_set) *is_set = 1;
	switch (l.unit) {
		case LAM_LEN_PX:  return (int32_t)lroundf(l.val);
		case LAM_LEN_PCT: return (int32_t)lroundf(l.val * 0.01f * (float)parent_px);
		case LAM_LEN_VW:  return (int32_t)lroundf(l.val * 0.01f * (float)lam_ui_screen_w);
		case LAM_LEN_VH:  return (int32_t)lroundf(l.val * 0.01f * (float)lam_ui_screen_h);
		case LAM_LEN_EM:  return (int32_t)lroundf(l.val * em_px);
		case LAM_LEN_REM: return (int32_t)lroundf(l.val * 16.0f);
		case LAM_LEN_CALC: {
			lam_len_t base = { l.calc_unit, l.val, LAM_LEN_AUTO, 0 };
			return resolve_len(base, parent_px, em_px, NULL) + (int32_t)lroundf(l.calc_px);
		}
		default:
			if (is_set) *is_set = 0;
			return LAM_UI_AUTO;
	}
}

static int32_t resolve_style_w(const lam_ui_style_t *s, int32_t parent_w, int *has)
{
	if (s->width_len.unit != LAM_LEN_AUTO) {
		int32_t v = resolve_len(s->width_len, parent_w, style_em_px(s), has);
		return v < 0 ? 0 : v;
	}
	if (s->width != LAM_UI_AUTO) {
		if (has) *has = 1;
		return s->width < 0 ? 0 : s->width;
	}
	if (has) *has = 0;
	return LAM_UI_AUTO;
}

static int32_t resolve_style_h(const lam_ui_style_t *s, int32_t parent_h, int *has)
{
	if (s->height_len.unit != LAM_LEN_AUTO) {
		int32_t v = resolve_len(s->height_len, parent_h, style_em_px(s), has);
		return v < 0 ? 0 : v;
	}
	if (s->height != LAM_UI_AUTO) {
		if (has) *has = 1;
		return s->height < 0 ? 0 : s->height;
	}
	if (has) *has = 0;
	return LAM_UI_AUTO;
}

static int32_t resolve_edge(lam_len_t len, int32_t legacy_px, int32_t ref_px, float em_px, int *has)
{
	if (len.unit != LAM_LEN_AUTO)
		return resolve_len(len, ref_px, em_px, has);
	if (legacy_px != LAM_UI_AUTO) {
		if (has) *has = 1;
		return legacy_px;
	}
	if (has) *has = 0;
	return LAM_UI_AUTO;
}

static int32_t clamp_min_max(int32_t val, lam_len_t min_l, lam_len_t max_l,
                             int32_t parent_px, float em_px)
{
	int has = 0;
	if (max_l.unit != LAM_LEN_AUTO) {
		int32_t mx = resolve_len(max_l, parent_px, em_px, &has);
		if (has && mx >= 0 && val > mx) val = mx;
	}
	if (min_l.unit != LAM_LEN_AUTO) {
		int32_t mn = resolve_len(min_l, parent_px, em_px, &has);
		if (has && mn >= 0 && val < mn) val = mn;
	}
	return val < 0 ? 0 : val;
}

/* ── Tree manipulation & DOM metadata ───────────────────────────────────── */

static void lam_ui_invalidate_up(lam_ui_div_t *d)
{
	for (lam_ui_div_t *p = d; p; p = p->parent) {
		lam_cache_invalidate(p);
		p->needs_layout = 1;
		lam_ui_div_t *pp = p->parent;
		if (!pp) continue;
		if (pp->style->display != LAM_UI_DISPLAY_FLEX &&
		    pp->style->display != LAM_UI_DISPLAY_INLINE_FLEX) {
			p->w = LAM_UI_AUTO;
			p->h = LAM_UI_AUTO;
		}
	}
}

void lam_ui_mark_dirty(lam_ui_div_t *d)
{
	if (!d) return;
	d->w = d->h = LAM_UI_AUTO;
	lam_ui_invalidate_up(d);
	for (lam_ui_div_t *c = d->first_child; c; c = c->next_sibling)
		lam_ui_mark_dirty(c);
}

lam_ui_div_t *lam_ui_new(lam_ui_div_t *parent, lam_ui_style_t *style)
{
	lam_ui_div_t *d = calloc(1, sizeof(*d));
	if (!d) return NULL;

	if (style) {
		d->style = style;
		d->style_calloc = 0;
		d->inline_override = calloc(1, sizeof(lam_ui_style_t));
		if (d->inline_override)
			*d->inline_override = *style;
	} else {
		d->style = calloc(1, sizeof(lam_ui_style_t));
		d->style_calloc = 1;
		lam_ui_style_reset(d->style);
	}

	d->w = d->h = LAM_UI_AUTO;

	if (!parent)
		return d;

	d->parent = parent;
	if (!parent->first_child) {
		parent->first_child = d;
		parent->last_child = d;
	} else {
		parent->last_child->next_sibling = d;
		parent->last_child = d;
	}

	lam_ui_mark_dirty(parent);
	return d;
}

void lam_ui_class(lam_ui_div_t *div, const char *class_name)
{
	if (!div) return;
	free(div->class_owned);
	div->class_owned = xstrdup(class_name);
	div->class_name = div->class_owned;
}

void lam_ui_text(lam_ui_div_t *div, const char *text)
{
	if (!div) return;
	free(div->text_owned);
	div->text_owned = xstrdup(text);
	div->text = div->text_owned;
	lam_ui_mark_dirty(div);
}

void lam_ui_set_tag(lam_ui_div_t *div, const char *tag)
{
	if (!div) return;
	free(div->tag);
	div->tag = xstrdup(tag);
}

void lam_ui_set_id(lam_ui_div_t *div, const char *id)
{
	if (!div) return;
	free(div->id);
	div->id = xstrdup(id);
}

void lam_ui_set_attr(lam_ui_div_t *div, const char *name, const char *val)
{
	if (!div || !name) return;
	for (uint16_t i = 0; i < div->n_attrs; i++) {
		if (strcmp(div->attrs[i].name, name) == 0) {
			free(div->attrs[i].val);
			div->attrs[i].val = xstrdup(val ? val : "");
			return;
		}
	}
	if (div->n_attrs >= div->cap_attrs) {
		uint16_t nc = div->cap_attrs ? (uint16_t)(div->cap_attrs * 2) : 4;
		lam_ui_attr_t *na = realloc(div->attrs, nc * sizeof(*na));
		if (!na) return;
		div->attrs = na;
		div->cap_attrs = nc;
	}
	div->attrs[div->n_attrs].name = xstrdup(name);
	div->attrs[div->n_attrs].val  = xstrdup(val ? val : "");
	div->n_attrs++;
}

void lam_ui_remove_attr(lam_ui_div_t *div, const char *name)
{
	if (!div || !name) return;
	for (uint16_t i = 0; i < div->n_attrs; i++) {
		if (strcmp(div->attrs[i].name, name) == 0) {
			free(div->attrs[i].name);
			free(div->attrs[i].val);
			for (uint16_t j = (uint16_t)(i + 1); j < div->n_attrs; j++)
				div->attrs[j - 1] = div->attrs[j];
			div->n_attrs--;
			lam_ui_mark_dirty(div);
			return;
		}
	}
}

void lam_ui_remove_child(lam_ui_div_t *parent, lam_ui_div_t *child)
{
	if (!parent || !child || child->parent != parent) return;
	lam_ui_div_t *prev = NULL;
	for (lam_ui_div_t *c = parent->first_child; c; c = c->next_sibling) {
		if (c == child) {
			if (prev)
				prev->next_sibling = child->next_sibling;
			else
				parent->first_child = child->next_sibling;
			if (parent->last_child == child)
				parent->last_child = prev;
			child->parent = NULL;
			child->next_sibling = NULL;
			lam_ui_mark_dirty(parent);
			return;
		}
		prev = c;
	}
}

void lam_ui_insert_before(lam_ui_div_t *parent, lam_ui_div_t *child, lam_ui_div_t *before)
{
	if (!parent || !child || child == before) return;
	if (child->parent)
		lam_ui_remove_child(child->parent, child);

	child->parent = parent;
	if (before && before->parent == parent) {
		lam_ui_div_t *prev = NULL;
		for (lam_ui_div_t *c = parent->first_child; c; c = c->next_sibling) {
			if (c == before) {
				child->next_sibling = before;
				if (prev)
					prev->next_sibling = child;
				else
					parent->first_child = child;
				lam_ui_mark_dirty(parent);
				return;
			}
			prev = c;
		}
	}

	child->next_sibling = NULL;
	if (!parent->first_child) {
		parent->first_child = child;
		parent->last_child = child;
	} else {
		parent->last_child->next_sibling = child;
		parent->last_child = child;
	}
	lam_ui_mark_dirty(parent);
}

void lam_ui_set_inline_style(lam_ui_div_t *div, const char *css_decls)
{
	if (!div) return;
	free(div->inline_style);
	div->inline_style = xstrdup(css_decls);
	lam_ui_mark_dirty(div);
}

void lam_ui_set_pseudo(lam_ui_div_t *div, uint32_t pseudo_mask)
{
	if (!div) return;
	div->pseudo_mask = pseudo_mask;
}

uint32_t lam_ui_get_pseudo(const lam_ui_div_t *div)
{
	return div ? div->pseudo_mask : 0;
}

void lam_ui_set_scroll(lam_ui_div_t *div, int32_t sx, int32_t sy)
{
	if (!div) return;
	div->scroll_x = sx < 0 ? 0 : sx;
	div->scroll_y = sy < 0 ? 0 : sy;
}

void lam_ui_get_scroll(const lam_ui_div_t *div, int32_t *sx, int32_t *sy)
{
	if (sx) *sx = div ? div->scroll_x : 0;
	if (sy) *sy = div ? div->scroll_y : 0;
}

void lam_ui_set_custom_draw(lam_ui_div_t *div, lam_ui_custom_draw_fn fn, void *user)
{
	if (!div) return;
	div->custom_draw = fn;
	div->custom_draw_user = user;
}

void lam_ui_set_user_data(lam_ui_div_t *div, void *user)
{
	if (!div) return;
	div->user_data = user;
}

void *lam_ui_get_user_data(const lam_ui_div_t *div)
{
	return div ? div->user_data : NULL;
}

/* ── Declarative builders ───────────────────────────────────────────────── */

lam_ui_div_t *lam_ui_flex(lam_ui_div_t *parent, lam_ui_flex_direction_t dir)
{
	lam_ui_div_t *d = lam_ui_new(parent, NULL);
	if (d) {
		LAM_UI_STYLE(d, display, LAM_UI_DISPLAY_FLEX);
		LAM_UI_STYLE(d, flex_direction, dir);
	}
	return d;
}

lam_ui_div_t *lam_ui_box(lam_ui_div_t *parent, int32_t w, int32_t h, uint32_t bg_color)
{
	lam_ui_div_t *d = lam_ui_new(parent, NULL);
	if (d) {
		LAM_UI_STYLE(d, width, w);
		LAM_UI_STYLE(d, height, h);
		LAM_UI_STYLE(d, background_color, bg_color);
	}
	return d;
}

lam_ui_div_t *lam_ui_label(lam_ui_div_t *parent, const char *text, uint32_t font_ref, uint32_t color)
{
	lam_ui_div_t *d = lam_ui_new(parent, NULL);
	if (d) {
		lam_ui_text(d, text);
		LAM_UI_STYLE(d, font_family_ref, font_ref);
		LAM_UI_STYLE(d, color, color);
	}
	return d;
}

void lam_ui_set_size(lam_ui_div_t *div, int32_t w, int32_t h)
{
	if (!div || !div->style) return;
	LAM_UI_STYLE(div, width, w);
	LAM_UI_STYLE(div, height, h);
}

void lam_ui_set_bg(lam_ui_div_t *div, uint32_t color)
{
	if (!div || !div->style) return;
	LAM_UI_STYLE(div, background_color, color);
}

void lam_ui_set_padding(lam_ui_div_t *div, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom)
{
	if (!div || !div->style) return;
	LAM_UI_STYLE(div, padding_left, left);
	LAM_UI_STYLE(div, padding_top, top);
	LAM_UI_STYLE(div, padding_right, right);
	LAM_UI_STYLE(div, padding_bottom, bottom);
}

void lam_ui_set_flex(lam_ui_div_t *div, float grow, float shrink, int32_t basis)
{
	if (!div || !div->style) return;
	LAM_UI_STYLE(div, flex_grow, grow);
	LAM_UI_STYLE(div, flex_shrink, shrink);
	LAM_UI_STYLE(div, flex_basis, basis);
}

void lam_ui_set_align(lam_ui_div_t *div, lam_ui_align_mode_t align_items, lam_ui_justify_content_mode_t justify_content)
{
	if (!div || !div->style) return;
	LAM_UI_STYLE(div, align_items, align_items);
	LAM_UI_STYLE(div, justify_content, justify_content);
}

/* ── Font measurement dispatch (TTF px vs legacy bitmap scale) ──────────── */

static const char *measure_text_auto(uint32_t *tw, uint32_t *th,
                                     const lam_ui_style_t *s, const char *text,
                                     uint32_t max_w, uint32_t max_h)
{
	if (lam_font_is_ttf(s->font_family_ref)) {
		uint32_t px = s->font_size_is_px ? s->font_size : 0;
		return lam_font_measure_px(tw, th, s->font_family_ref, text,
		                           0, 0, max_w, max_h, px,
		                           s->white_space, s->word_break);
	}
	return lam_font_measure(tw, th, s->font_family_ref, text,
	                        0, 0, max_w, max_h, s->font_size,
	                        s->white_space, s->word_break);
}

static const char *draw_text_auto(const lam_ui_style_t *s, const char *text,
                                  uint32_t x0, uint32_t y0,
                                  uint32_t x1, uint32_t y1)
{
	if (lam_font_is_ttf(s->font_family_ref)) {
		uint32_t px = s->font_size_is_px ? s->font_size : 0;
		return lam_font_draw_px(s->font_family_ref, text,
		                        x0, y0, x1, y1, px,
		                        s->white_space, s->word_break);
	}
	return lam_font_draw(s->font_family_ref, text,
	                     x0, y0, x1, y1, s->font_size,
	                     s->white_space, s->word_break);
}

static inline int is_flex_row(lam_ui_flex_direction_t d)
{
	return d == LAM_UI_ROW || d == LAM_UI_ROW_REVERSE;
}

static inline int is_flex_reverse(lam_ui_flex_direction_t d)
{
	return d == LAM_UI_ROW_REVERSE || d == LAM_UI_COLUMN_REVERSE;
}

static inline int is_out_of_flow(const lam_ui_style_t *s)
{
	return s->position == LAM_UI_POSITION_ABSOLUTE ||
	       s->position == LAM_UI_POSITION_FIXED;
}

static int gather_effective_children(lam_ui_div_t *parent, lam_ui_div_t **out, int max_n)
{
	int n = 0;
	for (lam_ui_div_t *c = parent->first_child; c && n < max_n; c = c->next_sibling) {
		if (!c->style || c->style->display == LAM_UI_DISPLAY_NONE)
			continue;
		if (c->style->display == LAM_UI_DISPLAY_CONTENTS)
			n += gather_effective_children(c, out + n, max_n - n);
		else
			out[n++] = c;
	}
	return n;
}

/* ── Content size measurement ───────────────────────────────────────────── */

static void measure_content_size_ex(lam_ui_div_t *c, uint32_t avail_w, uint32_t avail_h,
                                    int pct_w_def, int pct_h_def)
{
	const lam_ui_style_t *s = c->style;
	uint32_t inner_w = 0, inner_h = 0;

	if (pct_w_def && pct_h_def && !c->needs_layout &&
	    c->measured_w == avail_w && c->measured_h == avail_h) {
		c->content_w = c->measured_cw;
		c->content_h = c->measured_ch;
		return;
	}

	uint32_t h_chrome = s->padding_left + s->padding_right + s->border_width * 2;
	uint32_t v_chrome = s->padding_top + s->padding_bottom + s->border_width * 2;

	int has_w = 0, has_h = 0;
	int32_t exp_w = 0, exp_h = 0;
	if (!(s->width_len.unit == LAM_LEN_PCT && !pct_w_def))
		exp_w = resolve_style_w(s, (int32_t)avail_w, &has_w);
	if (!(s->height_len.unit == LAM_LEN_PCT && !pct_h_def))
		exp_h = resolve_style_h(s, (int32_t)avail_h, &has_h);

	uint32_t child_avail_w = has_w
		? ((uint32_t)exp_w > h_chrome ? (uint32_t)exp_w - h_chrome : 0)
		: (avail_w > h_chrome ? avail_w - h_chrome : 0);
	uint32_t child_avail_h = has_h
		? ((uint32_t)exp_h > v_chrome ? (uint32_t)exp_h - v_chrome : 0)
		: (avail_h > v_chrome ? avail_h - v_chrome : 0);
	int child_pct_w = has_w;
	int child_pct_h = has_h;

	lam_ui_div_t *kids[MAX_CHILDREN];
	int n_kids = gather_effective_children(c, kids, MAX_CHILDREN);

	int vis_count = 0;
	if (s->display == LAM_UI_DISPLAY_GRID) {
		uint32_t cols = s->grid_cols ? s->grid_cols : 1;
		uint32_t col_w = s->grid_col_px;
		uint32_t row_h = s->grid_row_px;
		for (int i = 0; i < n_kids; i++) {
			lam_ui_div_t *ch = kids[i];
			if (is_out_of_flow(ch->style))
				continue;
			measure_content_size_ex(ch, col_w ? col_w : child_avail_w, child_avail_h,
			                        col_w ? 1 : child_pct_w, row_h ? 1 : child_pct_h);
			if (ch->content_w > col_w && !s->grid_col_px) col_w = ch->content_w;
			if (ch->content_h > row_h && !s->grid_row_px) row_h = ch->content_h;
			vis_count++;
		}
		if (vis_count > 0) {
			uint32_t rows = ((uint32_t)vis_count + cols - 1) / cols;
			inner_w = cols * col_w + (cols > 1 ? (cols - 1) * s->column_gap : 0);
			inner_h = rows * row_h + (rows > 1 ? (rows - 1) * s->row_gap : 0);
		}
	} else {
		int is_row = (s->display == LAM_UI_DISPLAY_FLEX ||
		              s->display == LAM_UI_DISPLAY_INLINE_FLEX) &&
		             is_flex_row(s->flex_direction);

		for (int i = 0; i < n_kids; i++) {
			lam_ui_div_t *ch = kids[i];
			if (is_out_of_flow(ch->style))
				continue;

			measure_content_size_ex(ch, child_avail_w, child_avail_h,
			                        child_pct_w, child_pct_h);

			uint32_t cw = ch->content_w +
				(uint32_t)(ch->style->margin_left > 0 ? ch->style->margin_left : 0) +
				(uint32_t)(ch->style->margin_right > 0 ? ch->style->margin_right : 0);
			uint32_t chh = ch->content_h +
				(uint32_t)(ch->style->margin_top > 0 ? ch->style->margin_top : 0) +
				(uint32_t)(ch->style->margin_bottom > 0 ? ch->style->margin_bottom : 0);

			if (is_row) {
				if (vis_count > 0) inner_w += s->column_gap;
				inner_w += cw;
				if (chh > inner_h) inner_h = chh;
			} else {
				if (vis_count > 0) inner_h += s->row_gap;
				inner_h += chh;
				if (cw > inner_w) inner_w = cw;
			}
			vis_count++;
		}
	}

	if (c->text && *c->text && s->font_family_ref != CM_MISS) {
		uint32_t tw = 0, th = 0;
		uint32_t mw = child_avail_w ? child_avail_w : lam_ui_screen_w;
		uint32_t mh = child_avail_h ? child_avail_h : lam_ui_screen_h;
		measure_text_auto(&tw, &th, s, c->text, mw, mh);
		if (tw > inner_w) inner_w = tw;
		if (th > inner_h) inner_h = th;
	}

	inner_w += h_chrome;
	inner_h += v_chrome;

	if (has_w) inner_w = (uint32_t)exp_w;
	if (has_h) inner_h = (uint32_t)exp_h;

	inner_w = (uint32_t)clamp_min_max((int32_t)inner_w, s->min_width, s->max_width,
	                                  (int32_t)avail_w, style_em_px(s));
	inner_h = (uint32_t)clamp_min_max((int32_t)inner_h, s->min_height, s->max_height,
	                                  (int32_t)avail_h, style_em_px(s));

	c->content_w = inner_w;
	c->content_h = inner_h;
	if (pct_w_def && pct_h_def) {
		c->measured_w  = avail_w;
		c->measured_h  = avail_h;
		c->measured_cw = inner_w;
		c->measured_ch = inner_h;
	}
}

static void measure_content_size(lam_ui_div_t *c, uint32_t avail_w, uint32_t avail_h)
{
	measure_content_size_ex(c, avail_w, avail_h, 1, 1);
}

static void measure_text_overflow(lam_ui_div_t *c)
{
	c->overflow = NULL;
	if (!c->text || !*c->text || c->style->font_family_ref == CM_MISS)
		return;

	int32_t border_w = (int32_t)(c->style->border_width * 2);
	int32_t tw = c->w - (int32_t)c->style->padding_left - (int32_t)c->style->padding_right - border_w;
	int32_t th = c->h - (int32_t)c->style->padding_top  - (int32_t)c->style->padding_bottom - border_w;

	if (tw <= 0 || th <= 0) {
		c->overflow = c->text;
		return;
	}

	uint32_t mw = 0, mh = 0;
	c->overflow = measure_text_auto(&mw, &mh, c->style, c->text, (uint32_t)tw, (uint32_t)th);
}

/* ── Container inner box ────────────────────────────────────────────────── */

static void lam_ui_container_inner_box(lam_ui_div_t *container,
		float *x, float *y, float *w, float *h)
{
	const lam_ui_style_t *s = container->style;
	int32_t b = (int32_t)s->border_width;
	*x = (float)(container->x + (int32_t)s->padding_left + b);
	*y = (float)(container->y + (int32_t)s->padding_top  + b);
	*w = (float)(container->w - (int32_t)(s->padding_left + s->padding_right) - 2 * b);
	*h = (float)(container->h - (int32_t)(s->padding_top  + s->padding_bottom) - 2 * b);
	if (*w < 0.f) *w = 0.f;
	if (*h < 0.f) *h = 0.f;
}

static void apply_transform_offset(lam_ui_div_t *d)
{
	const lam_ui_style_t *s = d->style;
	if (s->transform_tx_px != 0.f || s->transform_tx_pct != 0.f)
		d->x += (int32_t)lroundf(s->transform_tx_px + s->transform_tx_pct * 0.01f * (float)d->w);
	if (s->transform_ty_px != 0.f || s->transform_ty_pct != 0.f)
		d->y += (int32_t)lroundf(s->transform_ty_px + s->transform_ty_pct * 0.01f * (float)d->h);
}

/* ── Flex layout (supports margin, gap, flex-wrap, min/max, reverse) ────── */

static int32_t item_main_margins(const lam_ui_div_t *ch, int row)
{
	return row ? (ch->style->margin_left + ch->style->margin_right)
	           : (ch->style->margin_top  + ch->style->margin_bottom);
}

static int32_t item_cross_margins(const lam_ui_div_t *ch, int row)
{
	return row ? (ch->style->margin_top  + ch->style->margin_bottom)
	           : (ch->style->margin_left + ch->style->margin_right);
}

static void flex_compute_line(lam_ui_container_t *ctx,
                              lam_ui_div_t **items, int count,
                              float line_origin_x, float line_origin_y,
                              float line_cross_avail,
                              float *out_line_cross, float *out_used_main)
{
	const lam_ui_style_t *cs = ctx->node->style;
	int row = is_flex_row(cs->flex_direction);
	float main_gap = (float)(row ? cs->column_gap : cs->row_gap);

	if (count <= 0) {
		*out_line_cross = 0.f;
		*out_used_main = 0.f;
		return;
	}

	/* 1. Compute base main size & cross size */
	float total_base = main_gap * (float)(count - 1);
	for (int i = 0; i < count; i++) {
		lam_ui_div_t *ch = items[i];
		const lam_ui_style_t *s = ch->style;
		int has_fb = 0, has_w = 0, has_h = 0;
		int32_t fb = resolve_edge(s->flex_basis_len, s->flex_basis,
		                          (int32_t)ctx->inner_main, style_em_px(s), &has_fb);
		int32_t ew = resolve_style_w(s, ctx->inner_w, &has_w);
		int32_t eh = resolve_style_h(s, ctx->inner_h, &has_h);

		float base_main;
		if (has_fb)
			base_main = (float)(fb < 0 ? 0 : fb);
		else if (row && has_w)
			base_main = (float)ew;
		else if (!row && has_h)
			base_main = (float)eh;
		else
			base_main = row ? (float)ch->content_w : (float)ch->content_h;

		if (row) {
			ch->w = clamp_min_max((int32_t)lroundf(base_main),
			                      s->min_width, s->max_width, ctx->inner_w, style_em_px(s));
			ch->h = has_h ? clamp_min_max(eh, s->min_height, s->max_height, ctx->inner_h, style_em_px(s))
			              : (int32_t)ch->content_h;
		} else {
			ch->h = clamp_min_max((int32_t)lroundf(base_main),
			                      s->min_height, s->max_height, ctx->inner_h, style_em_px(s));
			ch->w = has_w ? clamp_min_max(ew, s->min_width, s->max_width, ctx->inner_w, style_em_px(s))
			              : (int32_t)ch->content_w;
		}
		total_base += (float)(row ? ch->w : ch->h) + (float)item_main_margins(ch, row);
	}

	/* 2. Distribute free space (grow or shrink) */
	float free_space = ctx->inner_main - total_base;
	if (free_space > 0.f) {
		float total_grow = 0.f;
		for (int i = 0; i < count; i++)
			total_grow += items[i]->style->flex_grow;
		if (total_grow > 0.f) {
			for (int i = 0; i < count; i++) {
				lam_ui_div_t *ch = items[i];
				if (ch->style->flex_grow <= 0.f) continue;
				float add = (ch->style->flex_grow / total_grow) * free_space;
				if (row) {
					ch->w = clamp_min_max((int32_t)lroundf((float)ch->w + add),
					                      ch->style->min_width, ch->style->max_width,
					                      ctx->inner_w, style_em_px(ch->style));
				} else {
					ch->h = clamp_min_max((int32_t)lroundf((float)ch->h + add),
					                      ch->style->min_height, ch->style->max_height,
					                      ctx->inner_h, style_em_px(ch->style));
				}
			}
		}
	} else if (free_space < 0.f) {
		float total_scaled = 0.f;
		for (int i = 0; i < count; i++) {
			float b = (float)(row ? items[i]->w : items[i]->h);
			total_scaled += items[i]->style->flex_shrink * b;
		}
		if (total_scaled > 0.f) {
			for (int i = 0; i < count; i++) {
				lam_ui_div_t *ch = items[i];
				float b = (float)(row ? ch->w : ch->h);
				float sub = ((ch->style->flex_shrink * b) / total_scaled) * free_space;
				int32_t ns = (int32_t)lroundf(clampf(b + sub, 0.f, 1e9f));
				if (row) {
					ch->w = clamp_min_max(ns, ch->style->min_width, ch->style->max_width,
					                      ctx->inner_w, style_em_px(ch->style));
				} else {
					ch->h = clamp_min_max(ns, ch->style->min_height, ch->style->max_height,
					                      ctx->inner_h, style_em_px(ch->style));
				}
			}
		}
	}

	/* 3. Determine line cross size */
	float line_cross = 0.f;
	for (int i = 0; i < count; i++) {
		float cr = (float)(row ? items[i]->h : items[i]->w) + (float)item_cross_margins(items[i], row);
		if (cr > line_cross) line_cross = cr;
	}
	float eff_cross = (line_cross_avail > 0.f) ? line_cross_avail : line_cross;

	/* 4. Position along main axis */
	float used_main = main_gap * (float)(count - 1);
	int auto_margin_slots = 0;
	for (int i = 0; i < count; i++) {
		used_main += (float)(row ? items[i]->w : items[i]->h) + (float)item_main_margins(items[i], row);
		if (row) {
			if (items[i]->style->margin_left_auto)  auto_margin_slots++;
			if (items[i]->style->margin_right_auto) auto_margin_slots++;
		} else {
			if (items[i]->style->margin_top_auto)    auto_margin_slots++;
			if (items[i]->style->margin_bottom_auto) auto_margin_slots++;
		}
	}
	float rem = ctx->inner_main - used_main;
	if (rem < 0.f) rem = 0.f;

	float start = 0.f, extra_gap = 0.f, auto_m = 0.f;
	if (auto_margin_slots > 0) {
		auto_m = rem / (float)auto_margin_slots;
	} else {
		switch (cs->justify_content) {
			case LAM_UI_JUSTIFY_FLEX_START: start = 0.f; break;
			case LAM_UI_JUSTIFY_FLEX_END:   start = rem; break;
			case LAM_UI_JUSTIFY_CENTER:     start = rem * 0.5f; break;
			case LAM_UI_JUSTIFY_SPACE_BETWEEN:
				if (count > 1) extra_gap = rem / (float)(count - 1);
				break;
			case LAM_UI_JUSTIFY_SPACE_AROUND:
				extra_gap = rem / (float)count;
				start = extra_gap * 0.5f;
				break;
			case LAM_UI_JUSTIFY_SPACE_EVENLY:
				extra_gap = rem / (float)(count + 1);
				start = extra_gap;
				break;
		}
	}

	int rev = is_flex_reverse(cs->flex_direction);
	float cursor = start;
	for (int k = 0; k < count; k++) {
		int i = rev ? (count - 1 - k) : k;
		lam_ui_div_t *ch = items[i];
		const lam_ui_style_t *s = ch->style;

		float m_before = row ? (s->margin_left_auto  ? auto_m : (float)s->margin_left)
		                     : (s->margin_top_auto   ? auto_m : (float)s->margin_top);
		float m_after  = row ? (s->margin_right_auto ? auto_m : (float)s->margin_right)
		                     : (s->margin_bottom_auto? auto_m : (float)s->margin_bottom);

		cursor += m_before;
		if (row) {
			ch->x = (int32_t)lroundf(line_origin_x + cursor);
			cursor += (float)ch->w + m_after + main_gap + extra_gap;
		} else {
			ch->y = (int32_t)lroundf(line_origin_y + cursor);
			cursor += (float)ch->h + m_after + main_gap + extra_gap;
		}

		/* 5. Cross-axis alignment */
		lam_ui_align_mode_t align = (s->align_self != LAM_UI_ALIGN_AUTO)
			? s->align_self : cs->align_items;

		int has_cross = 0;
		if (row) resolve_style_h(s, ctx->inner_h, &has_cross);
		else     resolve_style_w(s, ctx->inner_w, &has_cross);

		float cm_before = (float)(row ? s->margin_top    : s->margin_left);
		float cm_after  = (float)(row ? s->margin_bottom : s->margin_right);

		if (align == LAM_UI_ALIGN_STRETCH && !has_cross) {
			int32_t stretched = (int32_t)lroundf(clampf(eff_cross - cm_before - cm_after, 0.f, 1e9f));
			if (row)
				ch->h = clamp_min_max(stretched, s->min_height, s->max_height, ctx->inner_h, style_em_px(s));
			else
				ch->w = clamp_min_max(stretched, s->min_width, s->max_width, ctx->inner_w, style_em_px(s));
		}

		float item_cross = (float)(row ? ch->h : ch->w);
		float offset = cm_before;
		if (align == LAM_UI_ALIGN_FLEX_END)
			offset = eff_cross - cm_after - item_cross;
		else if (align == LAM_UI_ALIGN_CENTER)
			offset = (eff_cross - item_cross) * 0.5f;

		if (row)
			ch->y = (int32_t)lroundf(line_origin_y + offset);
		else
			ch->x = (int32_t)lroundf(line_origin_x + offset);

		apply_transform_offset(ch);
	}

	*out_line_cross = line_cross;
	*out_used_main = used_main;
}

/* ── Forward declarations ───────────────────────────────────────────────── */

static void lam_ui_layout_dispatch(lam_ui_div_t *container);

/* ── Absolute & fixed positioning ───────────────────────────────────────── */

static void lam_ui_layout_out_of_flow(lam_ui_div_t *container)
{
	lam_ui_div_t *kids[MAX_CHILDREN];
	int n_kids = gather_effective_children(container, kids, MAX_CHILDREN);
	for (int i = 0; i < n_kids; i++) {
		lam_ui_div_t *a = kids[i];
		const lam_ui_style_t *as = a->style;
		if (!is_out_of_flow(as))
			continue;

		int is_fixed = (as->position == LAM_UI_POSITION_FIXED);
		int32_t rx = is_fixed ? 0 : container->x;
		int32_t ry = is_fixed ? 0 : container->y;
		int32_t rw = is_fixed ? (int32_t)lam_ui_screen_w : container->w;
		int32_t rh = is_fixed ? (int32_t)lam_ui_screen_h : container->h;
		float em = style_em_px(as);

		measure_content_size(a, (uint32_t)(rw > 0 ? rw : 0), (uint32_t)(rh > 0 ? rh : 0));

		int has_w = 0, has_h = 0, has_l = 0, has_r = 0, has_t = 0, has_b = 0;
		int32_t ew = resolve_style_w(as, rw, &has_w);
		int32_t eh = resolve_style_h(as, rh, &has_h);
		int32_t el = resolve_edge(as->left_len,   as->left,   rw, em, &has_l);
		int32_t er = resolve_edge(as->right_len,  as->right,  rw, em, &has_r);
		int32_t et = resolve_edge(as->top_len,    as->top,    rh, em, &has_t);
		int32_t eb = resolve_edge(as->bottom_len, as->bottom, rh, em, &has_b);

		a->w = has_w ? ew : (int32_t)a->content_w;
		a->h = has_h ? eh : (int32_t)a->content_h;

		if (has_l && has_r) {
			a->x = rx + el + as->margin_left;
			if (!has_w) {
				int32_t span = rw - el - er - as->margin_left - as->margin_right;
				a->w = span > 0 ? span : 0;
			}
		} else if (has_l) {
			a->x = rx + el + as->margin_left;
		} else if (has_r) {
			a->x = rx + rw - er - as->margin_right - a->w;
		} else {
			a->x = rx + as->margin_left;
		}

		if (has_t && has_b) {
			a->y = ry + et + as->margin_top;
			if (!has_h) {
				int32_t span = rh - et - eb - as->margin_top - as->margin_bottom;
				a->h = span > 0 ? span : 0;
			}
		} else if (has_t) {
			a->y = ry + et + as->margin_top;
		} else if (has_b) {
			a->y = ry + rh - eb - as->margin_bottom - a->h;
		} else {
			a->y = ry + as->margin_top;
		}

		a->w = clamp_min_max(a->w, as->min_width,  as->max_width,  rw, em);
		a->h = clamp_min_max(a->h, as->min_height, as->max_height, rh, em);

		apply_transform_offset(a);
	}
}

static void lam_ui_detect_overflow_and_recurse(lam_ui_div_t *container)
{
	if (container->style->display == LAM_UI_DISPLAY_NONE)
		return;
	measure_text_overflow(container);
	lam_ui_div_t *kids[MAX_CHILDREN];
	int n_kids = gather_effective_children(container, kids, MAX_CHILDREN);
	for (int i = 0; i < n_kids; i++)
		lam_ui_layout_dispatch(kids[i]);
}

/* ── Grid layout ────────────────────────────────────────────────────────── */

static void lam_ui_layout_grid(lam_ui_div_t *container)
{
	float inner_x, inner_y, inner_w, inner_h;
	const lam_ui_style_t *s = container->style;
	lam_ui_container_inner_box(container, &inner_x, &inner_y, &inner_w, &inner_h);

	uint32_t cols = s->grid_cols ? s->grid_cols : 1;
	uint32_t col_gap = s->column_gap;
	uint32_t row_gap = s->row_gap;

	float col_w = s->grid_col_px ? (float)s->grid_col_px
		: (inner_w - (float)((cols > 1 ? cols - 1 : 0) * col_gap)) / (float)cols;
	if (col_w < 1.f) col_w = 1.f;

	lam_ui_div_t *all_kids[MAX_CHILDREN];
	int n_all = gather_effective_children(container, all_kids, MAX_CHILDREN);
	lam_ui_div_t *items[MAX_CHILDREN];
	int n = 0;
	for (int i = 0; i < n_all && n < MAX_CHILDREN; i++) {
		lam_ui_div_t *c = all_kids[i];
		if (is_out_of_flow(c->style))
			continue;
		measure_content_size(c, (uint32_t)col_w, (uint32_t)inner_h);
		items[n++] = c;
	}

	float cy = inner_y;
	uint32_t rows = ((uint32_t)n + cols - 1) / cols;
	for (uint32_t r = 0; r < rows; r++) {
		float rh = s->grid_row_px ? (float)s->grid_row_px : 0.f;
		if (rh <= 0.f) {
			for (uint32_t col = 0; col < cols; col++) {
				uint32_t idx = r * cols + col;
				if ((int)idx >= n) break;
				int has_h = 0;
				int32_t eh = resolve_style_h(items[idx]->style, (int32_t)inner_h, &has_h);
				float h = has_h ? (float)eh : (float)items[idx]->content_h;
				if (h > rh) rh = h;
			}
			if (rh < 1.f) rh = col_w;
		}
		for (uint32_t col = 0; col < cols; col++) {
			uint32_t idx = r * cols + col;
			if ((int)idx >= n) break;
			lam_ui_div_t *ch = items[idx];
			ch->x = (int32_t)lroundf(inner_x + (float)col * (col_w + (float)col_gap));
			ch->y = (int32_t)lroundf(cy);
			ch->w = (int32_t)lroundf(col_w);
			ch->h = (int32_t)lroundf(rh);
			apply_transform_offset(ch);
		}
		cy += rh + (float)row_gap;
	}

	lam_ui_layout_out_of_flow(container);
	lam_ui_detect_overflow_and_recurse(container);
}

/* ── Flex container layout (single-line + flex-wrap) ────────────────────── */

static void lam_ui_layout_flex(lam_ui_div_t *container)
{
	float inner_x, inner_y, inner_w, inner_h;
	lam_ui_div_t *all_kids[MAX_CHILDREN];
	int n_all = gather_effective_children(container, all_kids, MAX_CHILDREN);
	lam_ui_div_t *flex_children[MAX_CHILDREN];
	int flex_n = 0;

	lam_ui_container_inner_box(container, &inner_x, &inner_y, &inner_w, &inner_h);

	for (int i = 0; i < n_all; i++) {
		lam_ui_div_t *c = all_kids[i];
		if (!is_out_of_flow(c->style)) {
			measure_content_size(c, (uint32_t)inner_w, (uint32_t)inner_h);
			if (flex_n < MAX_CHILDREN)
				flex_children[flex_n++] = c;
		}
	}

	lam_ui_container_t ctx;
	ctx.node        = container;
	ctx.inner_x     = (int32_t)inner_x;
	ctx.inner_y     = (int32_t)inner_y;
	ctx.inner_w     = (int32_t)inner_w;
	ctx.inner_h     = (int32_t)inner_h;
	int row = is_flex_row(container->style->flex_direction);
	ctx.inner_main  = row ? inner_w : inner_h;
	ctx.inner_cross = row ? inner_h : inner_w;

	float main_gap  = (float)(row ? container->style->column_gap : container->style->row_gap);
	float cross_gap = (float)(row ? container->style->row_gap    : container->style->column_gap);

	float cur_x = inner_x, cur_y = inner_y;
	float max_used_main = 0.f, total_cross = 0.f;

	if (container->style->flex_wrap == LAM_UI_WRAP_WRAP && flex_n > 0) {
		int start = 0, nlines = 0;
		while (start < flex_n) {
			float acc = 0.f;
			int end = start;
			while (end < flex_n) {
				lam_ui_div_t *ch = flex_children[end];
				int has_m = 0;
				int32_t em = row ? resolve_style_w(ch->style, ctx.inner_w, &has_m)
				                 : resolve_style_h(ch->style, ctx.inner_h, &has_m);
				float sz = (has_m ? (float)em : (float)(row ? ch->content_w : ch->content_h))
				         + (float)item_main_margins(ch, row);
				float add = (end > start ? main_gap : 0.f) + sz;
				if (end > start && acc + add > ctx.inner_main + 0.5f)
					break;
				acc += add;
				end++;
			}
			float lc = 0.f, um = 0.f;
			flex_compute_line(&ctx, flex_children + start, end - start,
			                  cur_x, cur_y, 0.f, &lc, &um);
			if (um > max_used_main) max_used_main = um;
			if (nlines > 0) total_cross += cross_gap;
			total_cross += lc;
			if (row) cur_y += lc + cross_gap;
			else     cur_x += lc + cross_gap;
			nlines++;
			start = end;
		}
	} else if (flex_n > 0) {
		float lc = 0.f, um = 0.f;
		flex_compute_line(&ctx, flex_children, flex_n,
		                  cur_x, cur_y, ctx.inner_cross, &lc, &um);
		max_used_main = um;
		total_cross = lc;
	}

	uint32_t h_chrome = container->style->padding_left + container->style->padding_right + 2 * container->style->border_width;
	uint32_t v_chrome = container->style->padding_top  + container->style->padding_bottom + 2 * container->style->border_width;
	if (row) {
		container->content_w = (uint32_t)max_used_main + h_chrome;
		container->content_h = (uint32_t)total_cross + v_chrome;
	} else {
		container->content_h = (uint32_t)max_used_main + v_chrome;
		container->content_w = (uint32_t)total_cross + h_chrome;
	}

	lam_ui_layout_out_of_flow(container);
	lam_ui_detect_overflow_and_recurse(container);
}

/* ── Block layout (supports explicit w/h, margins, inline siblings, gap) ── */

static void lam_ui_layout_block(lam_ui_div_t *container)
{
	float inner_x, inner_y, inner_w, inner_h;
	lam_ui_container_inner_box(container, &inner_x, &inner_y, &inner_w, &inner_h);

	lam_ui_div_t *kids[MAX_CHILDREN];
	int n_kids = gather_effective_children(container, kids, MAX_CHILDREN);

	for (int i = 0; i < n_kids; i++) {
		lam_ui_div_t *c = kids[i];
		if (!is_out_of_flow(c->style))
			measure_content_size(c, (uint32_t)inner_w, (uint32_t)inner_h);
	}

	float cursor_x = inner_x;
	float cursor_y = inner_y;
	float line_h   = 0.f;
	int32_t used_w = 0;

	for (int i = 0; i < n_kids; i++) {
		lam_ui_div_t *c = kids[i];
		const lam_ui_style_t *cs = c->style;
		if (!cs || is_out_of_flow(cs))
			continue;

		int is_inline = (cs->display == LAM_UI_DISPLAY_INLINE ||
		                 cs->display == LAM_UI_DISPLAY_INLINE_BLOCK ||
		                 cs->display == LAM_UI_DISPLAY_INLINE_FLEX);

		int has_w = 0, has_h = 0;
		int32_t ew = resolve_style_w(cs, (int32_t)inner_w, &has_w);
		int32_t eh = resolve_style_h(cs, (int32_t)inner_h, &has_h);

		if (is_inline) {
			c->w = has_w ? ew : (int32_t)c->content_w;
			c->h = has_h ? eh : (int32_t)c->content_h;
			c->w = clamp_min_max(c->w, cs->min_width,  cs->max_width,  (int32_t)inner_w, style_em_px(cs));
			c->h = clamp_min_max(c->h, cs->min_height, cs->max_height, (int32_t)inner_h, style_em_px(cs));

			float need_w = (float)(cs->margin_left + c->w + cs->margin_right);
			if (cursor_x > inner_x && cursor_x + need_w > inner_x + inner_w + 0.5f) {
				cursor_x = inner_x;
				cursor_y += line_h + (float)container->style->row_gap;
				line_h = 0.f;
			}
			c->x = (int32_t)lroundf(cursor_x) + cs->margin_left;
			c->y = (int32_t)lroundf(cursor_y) + cs->margin_top;
			cursor_x += need_w + (float)container->style->column_gap;
			float ih = (float)(cs->margin_top + c->h + cs->margin_bottom);
			if (ih > line_h) line_h = ih;
			if ((int32_t)(cursor_x - inner_x) > used_w) used_w = (int32_t)(cursor_x - inner_x);
		} else {
			if (line_h > 0.f) {
				cursor_x = inner_x;
				cursor_y += line_h + (float)container->style->row_gap;
				line_h = 0.f;
			}
			int32_t avail_cw = (int32_t)inner_w - cs->margin_left - cs->margin_right;
			if (avail_cw < 0) avail_cw = 0;

			c->w = has_w ? ew : avail_cw;
			c->h = has_h ? eh : (int32_t)c->content_h;
			c->w = clamp_min_max(c->w, cs->min_width,  cs->max_width,  (int32_t)inner_w, style_em_px(cs));
			c->h = clamp_min_max(c->h, cs->min_height, cs->max_height, (int32_t)inner_h, style_em_px(cs));

			int32_t xoff = cs->margin_left;
			if (cs->margin_left_auto && cs->margin_right_auto)
				xoff = ((int32_t)inner_w - c->w) / 2;
			else if (cs->margin_left_auto)
				xoff = (int32_t)inner_w - c->w - cs->margin_right;

			c->x = (int32_t)lroundf(inner_x) + xoff;
			c->y = (int32_t)lroundf(cursor_y) + cs->margin_top;
			cursor_y += (float)(cs->margin_top + c->h + cs->margin_bottom) + (float)container->style->row_gap;
			if (c->w > used_w) used_w = c->w;
		}
		apply_transform_offset(c);
	}

	if (line_h > 0.f)
		cursor_y += line_h;

	lam_ui_layout_out_of_flow(container);
	lam_ui_detect_overflow_and_recurse(container);

	container->content_w = (uint32_t)used_w +
		container->style->padding_left + container->style->padding_right +
		2 * container->style->border_width;
	container->content_h = (uint32_t)clampf(cursor_y - inner_y, 0.f, 1e9f) +
		container->style->padding_top + container->style->padding_bottom +
		2 * container->style->border_width;
}

static void lam_ui_layout_dispatch(lam_ui_div_t *container)
{
	if (!container || !container->style ||
	    container->style->display == LAM_UI_DISPLAY_NONE)
		return;
	if (container->w == LAM_UI_AUTO || container->h == LAM_UI_AUTO)
		return;

	switch (container->style->display) {
		case LAM_UI_DISPLAY_FLEX:
		case LAM_UI_DISPLAY_INLINE_FLEX:
			lam_ui_layout_flex(container);
			break;
		case LAM_UI_DISPLAY_GRID:
			lam_ui_layout_grid(container);
			break;
		default:
			lam_ui_layout_block(container);
			break;
	}
}

void lam_ui_layout(lam_ui_div_t *root)
{
	if (!root) return;
	if (!root->parent) {
		int has_w = 0, has_h = 0;
		int32_t rw = root->style ? resolve_style_w(root->style, (int32_t)lam_ui_screen_w, &has_w) : 0;
		int32_t rh = root->style ? resolve_style_h(root->style, (int32_t)lam_ui_screen_h, &has_h) : 0;
		root->x = 0;
		root->y = 0;
		root->w = (has_w && rw > 0) ? rw : (int32_t)lam_ui_screen_w;
		root->h = (has_h && rh > 0) ? rh : (int32_t)lam_ui_screen_h;
	}
	lam_ui_layout_dispatch(root);
	root->needs_layout = 0;
}

/* ── Z-index child sorting helper (shared by render and hit-test) ───────── */

static int collect_sorted_children(lam_ui_div_t *parent, lam_ui_div_t **out, int max_n)
{
	int n = gather_effective_children(parent, out, max_n);

	/* Stable insertion sort by z_index */
	for (int i = 1; i < n; i++) {
		lam_ui_div_t *key = out[i];
		int kz = key->style->z_index_set ? key->style->z_index : 0;
		int j = i;
		while (j > 0) {
			int pz = out[j - 1]->style->z_index_set ? out[j - 1]->style->z_index : 0;
			if (pz <= kz) break;
			out[j] = out[j - 1];
			j--;
		}
		out[j] = key;
	}
	return n;
}

/* ── Scissor clip helpers ───────────────────────────────────────────────── */

static clip_rect_t clip_intersect(clip_rect_t a, int32_t x, int32_t y, int32_t w, int32_t h)
{
	if (!a.active) {
		clip_rect_t r = { x, y, w > 0 ? w : 0, h > 0 ? h : 0, 1 };
		return r;
	}
	int32_t x0 = a.x > x ? a.x : x;
	int32_t y0 = a.y > y ? a.y : y;
	int32_t x1 = (a.x + a.w) < (x + w) ? (a.x + a.w) : (x + w);
	int32_t y1 = (a.y + a.h) < (y + h) ? (a.y + a.h) : (y + h);
	clip_rect_t r = { x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0, 1 };
	return r;
}

static void apply_gl_clip(clip_rect_t c)
{
	if (!lam_has_gl()) return;
	if (!c.active) {
		glDisable(GL_SCISSOR_TEST);
		return;
	}
	float s = (g_view_fbo == g_fbo) ? lam_get_dpi_scale() : 1.0f;
	int32_t px0 = (int32_t)floorf((float)c.x * s);
	int32_t py0 = (int32_t)floorf((float)c.y * s);
	int32_t px1 = (int32_t)ceilf((float)(c.x + c.w) * s);
	int32_t py1 = (int32_t)ceilf((float)(c.y + c.h) * s);
	int32_t pw = px1 - px0;
	int32_t ph = py1 - py0;
	int32_t gl_y = (int32_t)g_view_h - py1;
	if (gl_y < 0) {
		ph += gl_y;
		gl_y = 0;
	}
	if (px0 < 0) {
		pw += px0;
		px0 = 0;
	}
	if (pw < 0) pw = 0;
	if (ph < 0) ph = 0;
	glEnable(GL_SCISSOR_TEST);
	glScissor(px0, gl_y, pw, ph);
}

/* ── Renderer ───────────────────────────────────────────────────────────── */

static void render_div_rec(lam_ui_div_t *d, int32_t off_x, int32_t off_y,
                           float eff_opacity, clip_rect_t clip)
{
	const lam_ui_style_t *s = d->style;
	if (!s || s->display == LAM_UI_DISPLAY_NONE)
		return;

	float op = eff_opacity * s->opacity;
	if (op <= 0.001f)
		return;

	int32_t dx = d->x + off_x;
	int32_t dy = d->y + off_y;
	int32_t dw = d->w;
	int32_t dh = d->h;

	int can_paint_self = (s->display != LAM_UI_DISPLAY_CONTENTS) &&
	                     (s->visibility != LAM_UI_VIS_HIDDEN);
	int visible_self = can_paint_self && (dw > 0 && dh > 0);

	if (dw <= 0 && dh <= 0) {
		if (!d->first_child)
			return;
	}

	/* Compute padding-box overflow clip once so custom_draw, text, and children all honor overflow */
	int do_clip = (s->display != LAM_UI_DISPLAY_CONTENTS) &&
	              (s->overflow_x != LAM_UI_OVERFLOW_VISIBLE ||
	               s->overflow_y != LAM_UI_OVERFLOW_VISIBLE) && (dw > 0 && dh > 0);
	clip_rect_t inner_clip = clip;
	if (do_clip) {
		int32_t blw = (int32_t)(s->border_left_width   ? s->border_left_width   : s->border_width);
		int32_t brw = (int32_t)(s->border_right_width  ? s->border_right_width  : s->border_width);
		int32_t btw = (int32_t)(s->border_top_width    ? s->border_top_width    : s->border_width);
		int32_t bbw = (int32_t)(s->border_bottom_width ? s->border_bottom_width : s->border_width);
		int32_t iw = dw - blw - brw;
		int32_t ih = dh - btw - bbw;
		if (iw < 0) iw = 0;
		if (ih < 0) ih = 0;
		inner_clip = clip_intersect(clip, dx + blw, dy + btw, iw, ih);
	}

	if (visible_self) {
		uint32_t has_radius =
			s->border_radius_top_left | s->border_radius_top_right |
			s->border_radius_bottom_right | s->border_radius_bottom_left;
		float max_r = (float)(dw < dh ? dw : dh) * 0.5f;
		float tl = clampf((float)s->border_radius_top_left, 0.f, max_r);
		float tr = clampf((float)s->border_radius_top_right, 0.f, max_r);
		float br = clampf((float)s->border_radius_bottom_right, 0.f, max_r);
		float bl = clampf((float)s->border_radius_bottom_left, 0.f, max_r);

		/* box-shadow */
		if (s->box_shadow_color && s->box_shadow_blur > 0.0f) {
			lam_box_shadow(mul_alpha(s->box_shadow_color, op),
			               dx, dy, (uint32_t)dw, (uint32_t)dh,
			               tl, tr, br, bl,
			               s->box_shadow_blur,
			               s->box_shadow_offset_x, s->box_shadow_offset_y);
		}

		/* backdrop-filter: blur(...) */
		if (s->backdrop_blur > 0.0f) {
			lam_backdrop_blur_round(dx, dy, (uint32_t)dw, (uint32_t)dh,
			                        tl, tr, br, bl, s->backdrop_blur);
		}

		/* background & uniform border */
		uint32_t bg = mul_alpha(s->background_color, op);
		uint32_t bc = mul_alpha(s->border_color, op);

		if (s->background_image_ref != CM_MISS) {
			lam_tex_draw_box(s->background_image_ref,
			                 dx, dy, (uint32_t)dw, (uint32_t)dh,
			                 tl, tr, br, bl,
			                 s->bg_size_mode, s->bg_blend_mode, bg, op);
			if (s->border_width > 0 && bc) {
				if (has_radius) {
					lam_border_radius_grad(0, 0, 0, 180.0f, bc,
					                       dx, dy, (uint32_t)dw, (uint32_t)dh,
					                       tl, tr, br, bl, (float)s->border_width);
				} else {
					uint32_t b = s->border_width;
					lam_fill(dx, dy, (uint32_t)dw, b, bc);
					lam_fill(dx, dy + dh - (int32_t)b, (uint32_t)dw, b, bc);
					lam_fill(dx, dy, b, (uint32_t)dh, bc);
					lam_fill(dx + dw - (int32_t)b, dy, b, (uint32_t)dh, bc);
				}
			}
		} else if (bg || (s->bg_grad_type && s->background_color2) || s->border_width) {
			uint32_t bg2 = s->bg_grad_type ? mul_alpha(s->background_color2, op) : bg;
			if (has_radius || s->bg_grad_type != 0) {
				lam_border_radius_grad(bg, bg2, s->bg_grad_type, s->bg_grad_angle,
				                       bc, dx, dy, (uint32_t)dw, (uint32_t)dh,
				                       tl, tr, br, bl, (float)s->border_width);
			} else {
				if (bg)
					lam_fill(dx, dy, (uint32_t)dw, (uint32_t)dh, bg);
				if (s->border_width) {
					uint32_t b = s->border_width;
					lam_fill(dx, dy, (uint32_t)dw, b, bc);
					lam_fill(dx, dy + dh - (int32_t)b, (uint32_t)dw, b, bc);
					lam_fill(dx, dy, b, (uint32_t)dh, bc);
					lam_fill(dx + dw - (int32_t)b, dy, b, (uint32_t)dh, bc);
				}
			}
		}

		/* per-side borders (e.g. border-bottom: 1px solid ...) */
		if (s->border_top_width && s->border_top_width != s->border_width)
			lam_fill(dx, dy, (uint32_t)dw, s->border_top_width,
			         mul_alpha(s->border_top_color ? s->border_top_color : s->border_color, op));
		if (s->border_bottom_width && s->border_bottom_width != s->border_width)
			lam_fill(dx, dy + dh - (int32_t)s->border_bottom_width, (uint32_t)dw, s->border_bottom_width,
			         mul_alpha(s->border_bottom_color ? s->border_bottom_color : s->border_color, op));
		if (s->border_left_width && s->border_left_width != s->border_width)
			lam_fill(dx, dy, s->border_left_width, (uint32_t)dh,
			         mul_alpha(s->border_left_color ? s->border_left_color : s->border_color, op));
		if (s->border_right_width && s->border_right_width != s->border_width)
			lam_fill(dx + dw - (int32_t)s->border_right_width, dy, s->border_right_width, (uint32_t)dh,
			         mul_alpha(s->border_right_color ? s->border_right_color : s->border_color, op));

		/* custom draw hook (<canvas>, <img>, #term, <input>) */
		if (d->custom_draw) {
			if (!do_clip || (inner_clip.w > 0 && inner_clip.h > 0)) {
				if (do_clip)
					apply_gl_clip(inner_clip);
				d->custom_draw(d, dx, dy, dw, dh, d->custom_draw_user);
			}
			apply_gl_clip(clip);
		}
	}

	/* CSS outline (drawn outside box; paints even if one dimension is 0px) */
	if (can_paint_self && (dw > 0 || dh > 0) &&
	    s->outline_width > 0 && (s->outline_color & 0xFF000000u)) {
		uint32_t oc = mul_alpha(s->outline_color, op);
		int32_t ow = (int32_t)s->outline_width;
		uint32_t has_radius =
			s->border_radius_top_left | s->border_radius_top_right |
			s->border_radius_bottom_right | s->border_radius_bottom_left;
		if (has_radius && dw > 0 && dh > 0) {
			float max_r = (float)(dw < dh ? dw : dh) * 0.5f;
			float tl = clampf((float)s->border_radius_top_left, 0.f, max_r);
			float tr = clampf((float)s->border_radius_top_right, 0.f, max_r);
			float br = clampf((float)s->border_radius_bottom_right, 0.f, max_r);
			float bl = clampf((float)s->border_radius_bottom_left, 0.f, max_r);
			lam_border_radius_grad(0, 0, 0, 180.0f, oc,
			                       dx - ow, dy - ow,
			                       (uint32_t)(dw + 2 * ow), (uint32_t)(dh + 2 * ow),
			                       tl + (float)ow, tr + (float)ow,
			                       br + (float)ow, bl + (float)ow, (float)ow);
		} else {
			lam_fill(dx - ow, dy - ow, (uint32_t)(dw + 2 * ow), (uint32_t)ow, oc);
			if (dh > 0) {
				lam_fill(dx - ow, dy + dh, (uint32_t)(dw + 2 * ow), (uint32_t)ow, oc);
				lam_fill(dx - ow, dy, (uint32_t)ow, (uint32_t)dh, oc);
				lam_fill(dx + dw, dy, (uint32_t)ow, (uint32_t)dh, oc);
			}
		}
	}

	if (visible_self) {
		/* text */
		if (d->text && *d->text && s->font_family_ref != CM_MISS) {
			if (!do_clip || (inner_clip.w > 0 && inner_clip.h > 0)) {
				if (do_clip)
					apply_gl_clip(inner_clip);
				int32_t bw = (int32_t)(s->border_width * 2);
				int32_t tx = dx + (int32_t)s->padding_left + (int32_t)s->border_width;
				int32_t ty = dy + (int32_t)s->padding_top  + (int32_t)s->border_width;
				int32_t tw = dw - (int32_t)(s->padding_left + s->padding_right) - bw;
				int32_t th = dh - (int32_t)(s->padding_top  + s->padding_bottom) - bw;

				if (tw > 0 && th > 0) {
					uint32_t mw = 0, mh = 0;
					measure_text_auto(&mw, &mh, s, d->text, (uint32_t)tw, (uint32_t)th);

					int32_t align_x = tx;
					if (s->text_align == LAM_UI_TEXT_ALIGN_CENTER ||
					    ((s->display == LAM_UI_DISPLAY_FLEX || s->display == LAM_UI_DISPLAY_INLINE_FLEX) &&
					     s->justify_content == LAM_UI_JUSTIFY_CENTER))
						align_x = tx + (tw - (int32_t)mw) / 2;
					else if (s->text_align == LAM_UI_TEXT_ALIGN_RIGHT)
						align_x = tx + (tw - (int32_t)mw);
					if (align_x < tx) align_x = tx;

					/* Vertically center single-line text when container is flex-centered */
					int32_t align_y = ty;
					if ((s->display == LAM_UI_DISPLAY_FLEX || s->display == LAM_UI_DISPLAY_INLINE_FLEX) &&
					    s->align_items == LAM_UI_ALIGN_CENTER && (int32_t)mh < th)
						align_y = ty + (th - (int32_t)mh) / 2;

					int32_t ix1 = tx + tw;
					int32_t iy1 = ty + th;
					if (align_x >= 0 && align_y >= 0 && ix1 > align_x && iy1 > align_y) {
						if (s->text_shadow_color & 0xFF000000u) {
							int32_t sx = align_x + (int32_t)s->text_shadow_x;
							int32_t sy = align_y + (int32_t)s->text_shadow_y;
							int32_t sx1 = ix1 + (s->text_shadow_x > 0 ? (int32_t)s->text_shadow_x : 0);
							int32_t sy1 = iy1 + (s->text_shadow_y > 0 ? (int32_t)s->text_shadow_y : 0);
							if (sx >= 0 && sy >= 0 && sx1 > sx && sy1 > sy) {
								float shadow_op = (s->text_shadow_x != 0 || s->text_shadow_y != 0)
									? (op * 0.72f) : op;
								lam_tint(mul_alpha(s->text_shadow_color, shadow_op));
								draw_text_auto(s, d->text,
								               (uint32_t)sx, (uint32_t)sy,
								               (uint32_t)sx1, (uint32_t)sy1);
							}
						}
						uint32_t tcol = s->color ? s->color : lam_default_tint;
						lam_tint(mul_alpha(tcol, op));
						draw_text_auto(s, d->text,
						               (uint32_t)align_x, (uint32_t)align_y,
						               (uint32_t)ix1, (uint32_t)iy1);
					}
					lam_tint(lam_default_tint);
				}
				if (do_clip)
					apply_gl_clip(clip);
			}
		}
	}

	if (!d->first_child)
		return;

	/* Push overflow clip if hidden/auto/scroll */
	clip_rect_t child_clip = do_clip ? inner_clip : clip;
	if (do_clip) {
		if (child_clip.w <= 0 || child_clip.h <= 0)
			return;
		apply_gl_clip(child_clip);
	}

	lam_ui_div_t *sorted[MAX_CHILDREN];
	int nc = collect_sorted_children(d, sorted, MAX_CHILDREN);
	for (int i = 0; i < nc; i++) {
		lam_ui_div_t *ch = sorted[i];
		int32_t cox = (ch->style->position == LAM_UI_POSITION_FIXED) ? 0 : (off_x - d->scroll_x);
		int32_t coy = (ch->style->position == LAM_UI_POSITION_FIXED) ? 0 : (off_y - d->scroll_y);
		render_div_rec(ch, cox, coy, op, child_clip);
	}

	if (do_clip)
		apply_gl_clip(clip);
}

void render_div_raw(lam_ui_div_t *d)
{
	clip_rect_t none = { 0, 0, 0, 0, 0 };
	render_div_rec(d, 0, 0, 1.0f, none);
	if (lam_has_gl())
		glDisable(GL_SCISSOR_TEST);
}

void lam_ui_render(lam_ui_div_t *root)
{
	if (!root) return;
	if (root->needs_layout)
		lam_ui_layout(root);
	if (!g_fbo)
		return;
	render_div_raw(root);
}

/* ── Hit-testing (reverse paint order, z-index, clipping, pointer-events) ─ */

static lam_ui_div_t *hit_test_rec(lam_ui_div_t *d, int32_t px, int32_t py,
                                  int32_t off_x, int32_t off_y, clip_rect_t clip)
{
	const lam_ui_style_t *s = d->style;
	if (!s || s->display == LAM_UI_DISPLAY_NONE)
		return NULL;

	int32_t dx = d->x + off_x;
	int32_t dy = d->y + off_y;
	int32_t dw = d->w;
	int32_t dh = d->h;

	int do_clip = (s->display != LAM_UI_DISPLAY_CONTENTS) &&
	              (s->overflow_x != LAM_UI_OVERFLOW_VISIBLE ||
	               s->overflow_y != LAM_UI_OVERFLOW_VISIBLE) && (dw > 0 && dh > 0);
	clip_rect_t child_clip = clip;
	if (do_clip) {
		child_clip = clip_intersect(clip, dx, dy, dw, dh);
		if (px < child_clip.x || py < child_clip.y ||
		    px >= child_clip.x + child_clip.w || py >= child_clip.y + child_clip.h)
			return NULL;
	}

	/* Check children in reverse paint order (topmost z-index first) */
	if (d->first_child) {
		lam_ui_div_t *sorted[MAX_CHILDREN];
		int nc = collect_sorted_children(d, sorted, MAX_CHILDREN);
		for (int i = nc - 1; i >= 0; i--) {
			lam_ui_div_t *ch = sorted[i];
			int32_t cox = (ch->style->position == LAM_UI_POSITION_FIXED) ? 0 : (off_x - d->scroll_x);
			int32_t coy = (ch->style->position == LAM_UI_POSITION_FIXED) ? 0 : (off_y - d->scroll_y);
			lam_ui_div_t *hit = hit_test_rec(ch, px, py, cox, coy, child_clip);
			if (hit) return hit;
		}
	}

	if (s->display == LAM_UI_DISPLAY_CONTENTS ||
	    s->visibility == LAM_UI_VIS_HIDDEN || s->pointer_events == LAM_UI_PE_NONE)
		return NULL;
	if (dw <= 0 || dh <= 0)
		return NULL;
	if (clip.active && (px < clip.x || py < clip.y ||
	                    px >= clip.x + clip.w || py >= clip.y + clip.h))
		return NULL;
	if (px >= dx && py >= dy && px < dx + dw && py < dy + dh)
		return d;

	return NULL;
}

lam_ui_div_t *lam_ui_hit_test(lam_ui_div_t *root, int32_t px, int32_t py)
{
	if (!root) return NULL;
	clip_rect_t none = { 0, 0, 0, 0, 0 };
	return hit_test_rec(root, px, py, 0, 0, none);
}

/* ── Cleanup & destruction ──────────────────────────────────────────────── */

static void free_div_fields(lam_ui_div_t *d)
{
	lam_cache_free_div(d);
	free(d->class_owned);  d->class_owned = NULL; d->class_name = NULL;
	free(d->text_owned);   d->text_owned  = NULL; d->text = NULL;
	free(d->tag);          d->tag = NULL;
	free(d->id);           d->id = NULL;
	free(d->inline_style); d->inline_style = NULL;
	for (uint16_t i = 0; i < d->n_attrs; i++) {
		free(d->attrs[i].name);
		free(d->attrs[i].val);
	}
	free(d->attrs);
	d->attrs = NULL;
	d->n_attrs = d->cap_attrs = 0;
	free(d->inline_override);
	d->inline_override = NULL;
}

void lam_ui_clear(lam_ui_div_t *root)
{
	if (!root) return;
	lam_ui_div_t *c = root->first_child;
	while (c) {
		lam_ui_div_t *n = c->next_sibling;
		lam_ui_clear(c);
		free_div_fields(c);
		if (c->style_calloc && c->style)
			free(c->style);
		free(c);
		c = n;
	}
	root->first_child = NULL;
	root->last_child = NULL;
	lam_ui_mark_dirty(root);
}

void lam_ui_destroy(lam_ui_div_t *root)
{
	if (!root) return;
	lam_ui_clear(root);
	free_div_fields(root);
	if (root->style_calloc && root->style)
		free(root->style);
	free(root);
}

/* ── Accessors ──────────────────────────────────────────────────────────── */

const char *lam_ui_overflow(const lam_ui_div_t *div) { return div ? div->overflow : NULL; }
int32_t  lam_ui_get_x(const lam_ui_div_t *div)      { return div ? div->x : 0; }
int32_t  lam_ui_get_y(const lam_ui_div_t *div)      { return div ? div->y : 0; }
int32_t  lam_ui_get_width(const lam_ui_div_t *div)  { return div ? div->w : 0; }
int32_t  lam_ui_get_height(const lam_ui_div_t *div) { return div ? div->h : 0; }
uint32_t lam_ui_get_content_width(const lam_ui_div_t *div)  { return div ? div->content_w : 0; }
uint32_t lam_ui_get_content_height(const lam_ui_div_t *div) { return div ? div->content_h : 0; }
const char *lam_ui_get_text(const lam_ui_div_t *div)  { return div ? div->text : NULL; }
const char *lam_ui_get_class(const lam_ui_div_t *div) { return div ? div->class_name : NULL; }
const char *lam_ui_get_tag(const lam_ui_div_t *div)   { return div ? div->tag : NULL; }
const char *lam_ui_get_id(const lam_ui_div_t *div)    { return div ? div->id : NULL; }
const lam_ui_style_t *lam_ui_get_style(const lam_ui_div_t *div) { return div ? div->style : NULL; }
lam_ui_div_t *lam_ui_parent(const lam_ui_div_t *div)       { return div ? div->parent : NULL; }
lam_ui_div_t *lam_ui_first_child(const lam_ui_div_t *div)  { return div ? div->first_child : NULL; }
lam_ui_div_t *lam_ui_next_sibling(const lam_ui_div_t *div) { return div ? div->next_sibling : NULL; }

const char *lam_ui_get_attr(const lam_ui_div_t *div, const char *name)
{
	if (!div || !name) return NULL;
	for (uint16_t i = 0; i < div->n_attrs; i++) {
		if (strcmp(div->attrs[i].name, name) == 0)
			return div->attrs[i].val;
	}
	return NULL;
}
