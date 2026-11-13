#include "./ui.h"
#include "./ui-cache.h"
#include "../include/ttypt/lam-font.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include <ttypt/corm.h>

#define MAX_CHILDREN 256

typedef struct lam_ui_container {
	lam_ui_div_t *node;
	int32_t inner_x, inner_y;
	int32_t inner_w, inner_h;
	float inner_main, inner_cross;
	float line_x, line_y;
} lam_ui_container_t;

static uint32_t g_screen_w, g_screen_h;

void style_init(void);

void lam_ui_init(uint32_t screen_w, uint32_t screen_h)
{
	g_screen_w = screen_w;
	g_screen_h = screen_h;
	(void)g_screen_w;
	(void)g_screen_h;
	style_init();
}

static void lam_ui_invalidate_up(lam_ui_div_t *d)
{
	for (lam_ui_div_t *p = d; p; p = p->parent) {
		lam_ui_div_t *pp;
		lam_cache_invalidate(p);
		p->needs_layout = 1;
		pp = p->parent;
		if (!pp)
			continue;
		if (pp->style->display != LAM_UI_DISPLAY_FLEX)
		{
			p->w = LAM_UI_AUTO;
			p->h = LAM_UI_AUTO;
		}
	}
}

void lam_ui_mark_dirty(lam_ui_div_t *d)
{
	d->w = d->h = LAM_UI_AUTO;
	lam_ui_invalidate_up(d);

	for (lam_ui_div_t *c = d->first_child;
			c;
			c = c->next_sibling)

		lam_ui_mark_dirty(c);
}

lam_ui_div_t *lam_ui_new(lam_ui_div_t *parent, lam_ui_style_t *style)
{
	lam_ui_div_t *d = calloc(1, sizeof(*d));

	if (!d)
		return NULL;

	if (style) {
		d->style = style;
		d->style_calloc = 0;
	} else {
		d->style = calloc(1, sizeof(lam_ui_style_t));
		d->style_calloc = 1;
		lam_ui_style_reset(d->style);
	}

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
	div->class_name = class_name;
}

void lam_ui_text(lam_ui_div_t *div, const char *text)
{
	div->text = text;
	lam_ui_mark_dirty(div);
}

lam_ui_div_t *lam_ui_flex(lam_ui_div_t *parent, lam_ui_flex_direction_t dir)
{
	lam_ui_div_t *d = lam_ui_new(parent, NULL);
	if (d) {
		d->style->display = LAM_UI_DISPLAY_FLEX;
		d->style->flex_direction = dir;
		lam_ui_mark_dirty(d);
	}
	return d;
}

lam_ui_div_t *lam_ui_box(lam_ui_div_t *parent, int32_t w, int32_t h, uint32_t bg_color)
{
	lam_ui_div_t *d = lam_ui_new(parent, NULL);
	if (d) {
		d->style->width = w;
		d->style->height = h;
		d->style->background_color = bg_color;
		lam_ui_mark_dirty(d);
	}
	return d;
}

lam_ui_div_t *lam_ui_label(lam_ui_div_t *parent, const char *text, uint32_t font_ref, uint32_t color)
{
	lam_ui_div_t *d = lam_ui_new(parent, NULL);
	if (d) {
		d->text = text;
		d->style->font_family_ref = font_ref;
		d->style->color = color;
		lam_ui_mark_dirty(d);
	}
	return d;
}

void lam_ui_set_size(lam_ui_div_t *div, int32_t w, int32_t h)
{
	if (!div || !div->style) return;
	div->style->width = w;
	div->style->height = h;
	lam_ui_mark_dirty(div);
}

void lam_ui_set_bg(lam_ui_div_t *div, uint32_t color)
{
	if (!div || !div->style) return;
	div->style->background_color = color;
	lam_ui_mark_dirty(div);
}

void lam_ui_set_padding(lam_ui_div_t *div, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom)
{
	if (!div || !div->style) return;
	div->style->padding_left = left;
	div->style->padding_top = top;
	div->style->padding_right = right;
	div->style->padding_bottom = bottom;
	lam_ui_mark_dirty(div);
}

void lam_ui_set_flex(lam_ui_div_t *div, float grow, float shrink, int32_t basis)
{
	if (!div || !div->style) return;
	div->style->flex_grow = grow;
	div->style->flex_shrink = shrink;
	div->style->flex_basis = basis;
	lam_ui_mark_dirty(div);
}

void lam_ui_set_align(lam_ui_div_t *div, lam_ui_align_mode_t align_items, lam_ui_justify_content_mode_t justify_content)
{
	if (!div || !div->style) return;
	div->style->align_items = align_items;
	div->style->justify_content = justify_content;
	lam_ui_mark_dirty(div);
}

/* helpers no topo do ficheiro */
static void measure_content_size(
		lam_ui_div_t *c,
		uint32_t avail_w,
		uint32_t avail_h)
{
	uint32_t inner_w = 0, inner_h = 0;
	int flex_zero;

	/* fast path: constraints unchanged and layout clean */
	if (!c->needs_layout &&
			c->measured_w == avail_w
			&& c->measured_h == avail_h)
	{
		c->content_w = c->measured_cw;
		c->content_h = c->measured_ch;
		return;
	}

	/* Skip boxes whose subtree is fully hidden */
	bool any_visible = false;
	for (lam_ui_div_t *ch = c->first_child; ch; ch = ch->next_sibling)
		if (ch->style->display != LAM_UI_DISPLAY_NONE)
			any_visible = true;

	if (!any_visible && (!c->text || c->style->font_family_ref == CM_MISS)) {
		c->content_w = c->content_h = 0;
		return;
	}

	/* 1. measure children and text */
	for (lam_ui_div_t *ch = c->first_child; ch; ch = ch->next_sibling) {
		if (ch->style->display == LAM_UI_DISPLAY_NONE)
			continue;

		measure_content_size(ch, avail_w, avail_h);

		if (c->style->flex_direction == LAM_UI_ROW) {
			inner_w += ch->content_w;
			if (ch->content_h > inner_h)
				inner_h = ch->content_h;
		} else {
			inner_h += ch->content_h;
			if (ch->content_w > inner_w)
				inner_w = ch->content_w;
		}
	}

	/* 2. measure text */
	if (!c->first_child && c->text && c->style->font_family_ref != CM_MISS)
	{
		uint32_t tw = 0, th = 0;
		lam_font_measure(&tw, &th,
				c->style->font_family_ref, c->text,
				0, 0, avail_w, avail_h,
				c->style->font_size,
				c->style->white_space,
				c->style->word_break);
		if (tw > inner_w) inner_w = tw;
		if (th > inner_h) inner_h = th;
	}

	/* 3. minimal fallback for basis < 0 without children or text */
	if (c->style->flex_basis < 0 && !c->first_child && !c->text) {
		if (c->parent && c->parent->style->flex_direction == LAM_UI_ROW)
			inner_w = inner_w ? inner_w : 1;
		else
			inner_h = inner_h ? inner_h : 1;
	}

	flex_zero = c->style->flex_basis == 0
		&& c->style->flex_grow > 0.0f;

	/* 4. flex_zero logic applied after measurement.
	 * Preserves cross axis measurement, but replaces
	 * main axix.
	 */
	if (flex_zero) {
		if (c->parent && c->parent->style->flex_direction == LAM_UI_ROW)
			inner_w = 1;
		else
			inner_h = 1; /* Overwrite main axis */
	}

	/* 5. padding */
	inner_w += c->style->padding_left + c->style->padding_right;
	inner_h += c->style->padding_top + c->style->padding_bottom;

	/* Add border size, as it's part of the border-box */
	if (c->style->border_width > 0) {
		inner_w += c->style->border_width * 2;
		inner_h += c->style->border_width * 2;
	}

	/* 6. Define w/h if base != 0 */
	if (!flex_zero) {
		if (c->w == LAM_UI_AUTO)
			c->w = inner_w;
		if (c->h == LAM_UI_AUTO)
			c->h = inner_h;
	}

	/* 7. Always store the measurement result */
	c->content_w = inner_w;
	c->content_h = inner_h;

	/* store memo */
	c->measured_w  = avail_w;
	c->measured_h  = avail_h;
	c->measured_cw = inner_w;
	c->measured_ch = inner_h;
}

static void measure_text_overflow(lam_ui_div_t *c)
{
	c->overflow = NULL;

	if (!c->text || c->style->font_family_ref == CM_MISS)
		return;

	int32_t border_w = c->style->border_width * 2;
	int32_t border_h = c->style->border_width * 2;

	int32_t tw = (int32_t) c->w
		- c->style->padding_left
		- c->style->padding_right
		- border_w;

	int32_t th = (int32_t) c->h
		- c->style->padding_top
		- c->style->padding_bottom
		- border_h;

	if (tw <= 0 || th <= 0) {
		c->overflow = c->text; /* nothing fits */
		return;
	}

	uint32_t mw = 0, mh = 0;

	c->overflow = lam_font_measure(&mw, &mh,
			c->style->font_family_ref, c->text,
			0, 0, (uint32_t)tw, (uint32_t)th,
			c->style->font_size,
			c->style->white_space,
			c->style->word_break);
}

/*
 * clampf — constrain a float to [lo, hi]
 *
 * Returns:
 *   lo if v < lo
 *   hi if v > hi
 *   v otherwise
 */
static inline float clampf(float v, float lo, float hi)
{
	if (v < lo)
		return lo;

	if (v > hi)
		return hi;

	return v;
}

/*
 * Step 1: Compute base sizes and hypothetical main sizes
 *
 * For each flex item:
 *  - flex-basis > 0   → use that value.
 *  - flex-basis == 0  → skip content measurement (base 0)
 *  - flex-basis < 0   → "auto": use width/height if
 *  	specified, otherwise measure intrinsic content.
 *
 * The result is stored in child->w or child->h
 * depending on container direction.
 */

static void compute_base_sizes(lam_ui_container_t *c, lam_ui_div_t **items, int count)
{
	for (int i = 0; i < count; i++) {
		lam_ui_div_t *child = items[i];

		if (child->style->display == LAM_UI_DISPLAY_NONE)
			continue;

		lam_ui_style_t *s = child->style;
		float base_main = 0.f;

		/* use measured content if it already exists */
		float content_main = c->node->style->flex_direction == LAM_UI_ROW
			? (float)child->content_w
			: (float)child->content_h;

		if (s->flex_basis > 0)
			base_main = (float)s->flex_basis;
		else if (s->flex_basis == 0)
			base_main = 0.f;
		else if (c->node->style->flex_direction == LAM_UI_ROW && s->width > 0)
			base_main = (float)s->width;
		else if (c->node->style->flex_direction == LAM_UI_COLUMN && s->height > 0)
			base_main = (float)s->height;
		else /* basis < 0 → auto */
			base_main = (content_main > 0)
				? content_main
				: 1.0f;

		/* don't overwrite if there are already
		 * valid w/h from measure_content_size
		 */
		if (c->node->style->flex_direction == LAM_UI_ROW) {
			if (child->w == 0 || child->w == LAM_UI_AUTO)
				child->w = (uint32_t)
					clampf(base_main, 1, 1e9f);
		} else {
			if (child->h == 0 || child->h == LAM_UI_AUTO)
				child->h = (uint32_t)
					clampf(base_main, 1, 1e9f);
		}
	}
}


/*
 * Grow phase: distribute positive free space by flex-grow
 */
static void distribute_flex_grow(lam_ui_container_t *c,
		lam_ui_div_t **items, int count, float free_space)
{
	float total_grow = 0.f;

	for (int i = 0; i < count; i++) {
		lam_ui_div_t *ch = items[i];

		if (ch->style->display == LAM_UI_DISPLAY_NONE)
			continue;

		total_grow += ch->style->flex_grow;
	}

	if (total_grow <= 0.f)
		return;

	for (int i = 0; i < count; i++) {
		lam_ui_div_t *ch = items[i];

		if (ch->style->display == LAM_UI_DISPLAY_NONE)
			continue;

		float add = (ch->style->flex_grow / total_grow) * free_space;

		if (c->node->style->flex_direction == LAM_UI_ROW)
			ch->w = (uint32_t) clampf((float)ch->w + add, 0, 1e9);
		else
			ch->h = (uint32_t) clampf((float)ch->h + add, 0, 1e9);
	}
}

/*
 * Shrink phase: distribute negative free space by flex-shrink × base
 */
static void distribute_flex_shrink(lam_ui_container_t *c,
		lam_ui_div_t **items, int count, float free_space)
{
	float total_scaled = 0.f;

	for (int i = 0; i < count; i++) {
		lam_ui_div_t *ch = items[i];

		if (ch->style->display == LAM_UI_DISPLAY_NONE)
			continue;

		float basis = (c->node->style->flex_direction == LAM_UI_ROW)
			? (float)ch->w : (float)ch->h;

		total_scaled += ch->style->flex_shrink * basis;
	}

	if (total_scaled <= 0.f)
		return;

	for (int i = 0; i < count; i++) {
		lam_ui_div_t *ch = items[i];
		if (ch->style->display == LAM_UI_DISPLAY_NONE)
			continue;

		float basis = (c->node->style->flex_direction == LAM_UI_ROW)
			? (float)ch->w : (float)ch->h;
		float scaled = ch->style->flex_shrink * basis;

		/* free_space < 0 */
		float sub = (scaled / total_scaled) * free_space;
		float new_size = basis + sub;

		if (new_size < 0.f)
			new_size = 0.f;

		if (c->node->style->flex_direction == LAM_UI_ROW)
			ch->w = (uint32_t)new_size;
		else
			ch->h = (uint32_t)new_size;
	}
}

/*
 * Step 2: Distribute free space (grow / flex_shrink)
 *
 * For each flex line:
 *  - Compute free_space = inner_main - Σ(base_main)
 *  - If free_space > 0  → distribute by flex-grow
 *  - If free_space < 0  → distribute by flex-shrink × base_main
 *  - Clamp results >= 0
 */
static void distribute_free_space(lam_ui_container_t *c,
		lam_ui_div_t **items, int count)
{
	float total_basis = 0.f;

	for (int i = 0; i < count; i++) {
		lam_ui_div_t *child = items[i];

		if (child->style->display == LAM_UI_DISPLAY_NONE)
			continue;

		if (c->node->style->flex_direction == LAM_UI_ROW)
			total_basis += (float)child->w;
		else
			total_basis += (float)child->h;
	}

	float free_space = c->inner_main - total_basis;

	if (free_space >= 0.f)
		distribute_flex_grow(c, items, count, free_space);
	else if (free_space < 0.f)
		distribute_flex_shrink(c, items, count, free_space);
}

/*
 * Step 3: Main-axis positioning (justify-content)
 *
 * After all items on this flex line have their final main sizes:
 *   - Sum total used space
 *   - Compute remaining free space
 *   - Distribute according to justify-content
 *
 * Works for both LAM_UI_ROW and LAM_UI_COLUMN directions.
 */

static void position_main_axis(lam_ui_container_t *c,
		lam_ui_div_t **items, int count,
		float line_main_origin_x,
		float line_main_origin_y)
{
	if (count == 0)
		return;

	/* 1) total used main size */
	float total_used = 0.f;
	for (int i = 0; i < count; i++) {
		lam_ui_div_t *ch = items[i];
		if (ch->style->display == LAM_UI_DISPLAY_NONE)
			continue;
		total_used += (c->node->style->flex_direction == LAM_UI_ROW)
			? (float)ch->w : (float)ch->h;
	}

	float free_space = c->inner_main - total_used;
	if (free_space < 0.f) free_space = 0.f; /* overflow handled earlier */

	/* 2) determine spacing and start offset */
	float start = 0.f, gap = 0.f;

	switch (c->node->style->justify_content) {
		case LAM_UI_JUSTIFY_FLEX_START:
			start = 0.f;
			break;

		case LAM_UI_JUSTIFY_FLEX_END:
			start = free_space;
			break;

		case LAM_UI_JUSTIFY_CENTER:
			start = free_space * 0.5f;
			break;

		case LAM_UI_JUSTIFY_SPACE_BETWEEN:
			if (count > 1)
				gap = free_space / (float)(count - 1);
			else
				start = free_space * 0.5f;
			break;

		case LAM_UI_JUSTIFY_SPACE_AROUND:
			gap = free_space / (float)count;
			start = gap * 0.5f;
			break;
	}

	/* 3) assign positions along main axis */
	float cursor = start;
	for (int i = 0; i < count; i++) {
		lam_ui_div_t *ch = items[i];
		if (ch->style->display == LAM_UI_DISPLAY_NONE)
			continue;

		if (c->node->style->flex_direction == LAM_UI_ROW) {
			ch->x = (int32_t)(line_main_origin_x + cursor);
			ch->y = (int32_t)line_main_origin_y;
			cursor += (float)ch->w + gap;
		} else {
			ch->x = (int32_t)line_main_origin_x;
			ch->y = (int32_t)(line_main_origin_y + cursor);
			cursor += (float)ch->h + gap;
		}
	}
}

/*
 * Step 4: Cross-axis positioning (align-items / align-self)
 *
 * For each item on the line:
 *   - Determine target cross size
 *   - Apply align-self (or container align-items)
 *   - Place item within the line’s cross area
 *
 * The line itself has already been assigned its cross origin
 * and cross size by the multi-line driver.
 */
static void position_cross_axis(
		lam_ui_container_t *c,
		lam_ui_div_t **items,
		int count)
{
	for (int i = 0; i < count; i++) {
		lam_ui_div_t *child = items[i];

		if (child->style->display == LAM_UI_DISPLAY_NONE)
			continue;

		lam_ui_style_t *s = child->style;

		/* 1) choose effective alignment */
		lam_ui_align_mode_t align
			= c->node->style->align_items;

		if (s->align_self != LAM_UI_ALIGN_AUTO)
			align = s->align_self;

		/* 2) compute available space on the cross axis */
		float cross_inner = (c->node->style->flex_direction == LAM_UI_ROW)
			? c->inner_h
			: c->inner_w;

		/* 3) stretch logic */
		if (align == LAM_UI_ALIGN_STRETCH) {
			if (c->node->style->flex_direction == LAM_UI_ROW)
				child->h = (uint32_t)clampf(cross_inner, 0.f, 1e9f);
			else
				child->w = (uint32_t)clampf(cross_inner, 0.f, 1e9f);
		}

		/* 4) determine item cross size */
		float item_cross = (c->node->style->flex_direction == LAM_UI_ROW)
			? (float)child->h
			: (float)child->w;

		float offset = 0.f;

		switch (align) {
			case LAM_UI_ALIGN_FLEX_END:
				offset = cross_inner - item_cross;
				break;
			case LAM_UI_ALIGN_CENTER:
				offset = (cross_inner - item_cross) * 0.5f;
				break;
			default:
				offset = 0.f;
				break;
		}


		/* 7) apply final position — fix: don't add padding twice */
		if (c->node->style->flex_direction == LAM_UI_ROW)
			child->y = (int32_t)(c->inner_y + offset);
		else
			child->x = (int32_t)(c->inner_x + offset);
	}
}

static void lam_ui_container_inner_box(lam_ui_div_t *container,
		float *x, float *y,
		float *w, float *h)
{
	const lam_ui_style_t *s = container->style;
	*x = container->x + s->padding_left;
	*y = container->y + s->padding_top;
	*w = container->w - (s->padding_left + s->padding_right);
	*h = container->h - (s->padding_top + s->padding_bottom);

	if (*w < 0)
		*w = 0;

	if (*h < 0)
		*h = 0;
}

static int lam_ui_collect_children(
		int *flex_n_r, int *abs_n_r,
		lam_ui_div_t *container,
		lam_ui_div_t **flex_out,
		lam_ui_div_t **abs_out)
{
	int flex_n = 0, abs_n = 0;

	for (lam_ui_div_t *c = container->first_child;
			c;
			c = c->next_sibling)
	{
		if (c->style->display == LAM_UI_DISPLAY_NONE)
			continue;

		if (c->style->position == LAM_UI_POSITION_ABSOLUTE)
			abs_out[abs_n++] = c;
		else
			flex_out[flex_n++] = c;
	}

	*abs_n_r = abs_n;
	*flex_n_r = flex_n;
	return flex_n;
}

static void lam_ui_flex_layout_line(struct lam_ui_container *ctx,
		struct lam_ui_div **items, int count,
		float *inner_x, float *inner_y)
{
	int i;
	float line_cross = 0.f;

	compute_base_sizes(ctx, items, count);
	distribute_free_space(ctx, items, count);

	/* no second measure pass */
	for (i = 0; i < count; i++) {
		struct lam_ui_div *ch = items[i];

		if (ch->style->display == LAM_UI_DISPLAY_NONE)
			continue;

		/* Only synthesize cross size for auto basis */
		if (ch->style->flex_basis < 0) {
			if (ctx->node->style->flex_direction == LAM_UI_ROW)
				ch->h = (ch->style->height > 0) ? (uint32_t)ch->style->height : ch->content_h;
			else
				ch->w = (ch->style->width > 0) ? (uint32_t)ch->style->width : ch->content_w;
		}
	}

	for (i = 0; i < count; i++) {
		float cross = (ctx->node->style->flex_direction == LAM_UI_ROW) ?
			(float)items[i]->h : (float)items[i]->w;
		if (cross > line_cross)
			line_cross = cross;
	}

	position_main_axis(ctx, items, count, *inner_x, *inner_y);
	position_cross_axis(ctx, items, count);

	if (ctx->node->style->flex_direction == LAM_UI_ROW)
		*inner_y += line_cross;
	else
		*inner_x += line_cross;

	/* update parent intrinsic */
	{
		float used_main = 0.f, used_cross = 0.f;

		for (i = 0; i < count; i++) {
			struct lam_ui_div *ch = items[i];
			if (ch->style->display == LAM_UI_DISPLAY_NONE)
				continue;

			if (ctx->node->style->flex_direction == LAM_UI_ROW) {
				used_main += (float)ch->w;
				if ((float)ch->h > used_cross)
					used_cross = (float)ch->h;
			} else {
				used_main += (float)ch->h;
				if ((float)ch->w > used_cross)
					used_cross = (float)ch->w;
			}
		}

		used_main += ctx->node->style->padding_left +
			ctx->node->style->padding_right +
			2 * ctx->node->style->border_width;

		used_cross += ctx->node->style->padding_top +
			ctx->node->style->padding_bottom +
			2 * ctx->node->style->border_width;

		if (ctx->node->style->flex_direction == LAM_UI_ROW) {
			ctx->node->content_w = (uint32_t)used_main;
			if (used_cross > ctx->node->content_h)
				ctx->node->content_h = (uint32_t)used_cross;
		} else {
			ctx->node->content_h = (uint32_t)used_main;
			if (used_cross > ctx->node->content_w)
				ctx->node->content_w = (uint32_t)used_cross;
		}
	}
}

/* Position absolute position children relative to container */
static void lam_ui_layout_absolute_children(
		lam_ui_div_t *container,
		lam_ui_div_t **abs_children)
{
	/* optional array  - recompute from the tree */
	(void) abs_children;

	for (lam_ui_div_t *a = container->first_child;
			a;
			a = a->next_sibling)
	{
		if (a->style->display == LAM_UI_DISPLAY_NONE)
			continue;

		if (a->style->position != LAM_UI_POSITION_ABSOLUTE)
			continue;

		lam_ui_style_t *as = a->style;

		if (as->width != LAM_UI_AUTO)
			a->w = (uint32_t)as->width;
		if (as->height != LAM_UI_AUTO)
			a->h = (uint32_t)as->height;

		/* X axis */
		if (as->left != LAM_UI_AUTO && as->right != LAM_UI_AUTO) {
			a->x = container->x + as->left;

			int32_t right_edge = container->x
				+ (int32_t)container->w - as->right;

			a->w = (uint32_t)
				clampf((float)(right_edge - a->x),
						0.f, 1e9f);
		} else if (as->left != LAM_UI_AUTO)
			a->x = container->x + as->left;
		else if (as->right != LAM_UI_AUTO)
			a->x = container->x + (int32_t)container->w - as->right - (int32_t)a->w;
		else
			a->x = container->x;

		/* Y axis */
		if (as->top != LAM_UI_AUTO && as->bottom != LAM_UI_AUTO) {
			a->y = container->y + as->top;

			int32_t bottom_edge = container->y
				+ (int32_t)container->h - as->bottom;

			a->h = (uint32_t)
				clampf((float)(bottom_edge - a->y),
						0.f, 1e9f);
		} else if (as->top != LAM_UI_AUTO)
			a->y = container->y + as->top;
		else if (as->bottom != LAM_UI_AUTO)
			a->y = container->y + (int32_t)container->h
				- as->bottom - (int32_t)a->h;
		else
			a->y = container->y;
	}
}

static void lam_ui_layout_flex(lam_ui_div_t *container);
static void lam_ui_layout_block(lam_ui_div_t *container);

static void lam_ui_layout_dispatch(struct lam_ui_div *container)
{
	if (!container || !container->style ||
	    container->style->display == LAM_UI_DISPLAY_NONE)
		return;

	if (container->w == LAM_UI_AUTO || container->h == LAM_UI_AUTO)
		return;	/* o pai (flex ou block) ainda não fixou caixa */

	if (container->style->display == LAM_UI_DISPLAY_FLEX)
		lam_ui_layout_flex(container);      /* formatação interna flex */
	else
		lam_ui_layout_block(container);     /* formatação interna block */
}

/* Measure overflow and do recursive child layout */
static void lam_ui_detect_overflow_and_recurse(lam_ui_div_t *container)
{
	if (container->style->display == LAM_UI_DISPLAY_NONE)
		return;

	measure_text_overflow(container);

	for (lam_ui_div_t *c = container->first_child;
			c;
			c = c->next_sibling)

		lam_ui_layout_dispatch(c);
}

static void lam_ui_layout_flex(struct lam_ui_div *container)
{
	float inner_x, inner_y, inner_w, inner_h;
	struct lam_ui_div *abs_children[MAX_CHILDREN];
	struct lam_ui_div *flex_children[MAX_CHILDREN];
	int flex_n, abs_n;
	struct lam_ui_container ctx;
	float cursor_x, cursor_y;
	struct lam_ui_div *c;

	if (container->style->display == LAM_UI_DISPLAY_NONE
			|| container->w == LAM_UI_AUTO
			|| container->h == LAM_UI_AUTO)
		return;

	lam_ui_container_inner_box(container, &inner_x, &inner_y, &inner_w, &inner_h);

	for (c = container->first_child; c; c = c->next_sibling)
		measure_content_size(c, (uint32_t)inner_w, (uint32_t)inner_h);

	lam_ui_collect_children(&flex_n, &abs_n, container, flex_children, abs_children);

	ctx.node        = container;
	ctx.inner_x     = inner_x;
	ctx.inner_y     = inner_y;
	ctx.inner_w     = inner_w;
	ctx.inner_h     = inner_h;
	ctx.inner_main  = (container->style->flex_direction == LAM_UI_ROW) ? inner_w : inner_h;
	ctx.inner_cross = (container->style->flex_direction == LAM_UI_ROW) ? inner_h : inner_w;

	cursor_x = inner_x;
	cursor_y = inner_y;
	lam_ui_flex_layout_line(&ctx, flex_children, flex_n, &cursor_x, &cursor_y);

	lam_ui_layout_absolute_children(container, abs_children);
	lam_ui_detect_overflow_and_recurse(container);
}

static void lam_ui_layout_block(lam_ui_div_t *container)
{
	float inner_x, inner_y, inner_w, inner_h;
	lam_ui_div_t *c;
	float cursor_y = 0.f;
	int32_t used_w = 0;

	if (!container || !container->style ||
	    container->style->display == LAM_UI_DISPLAY_NONE)
		return;

	if (container->w == LAM_UI_AUTO
			|| container->h == LAM_UI_AUTO)
		return;

	lam_ui_container_inner_box(container, &inner_x, &inner_y, &inner_w, &inner_h);

	cursor_y = inner_y;

	/* measure children first */
	for (c = container->first_child; c; c = c->next_sibling)
		measure_content_size(c, (uint32_t)inner_w, (uint32_t)inner_h);

	/* stack normal children vertically */
	for (c = container->first_child; c; c = c->next_sibling) {
		if (!c->style || c->style->display == LAM_UI_DISPLAY_NONE)
			continue;
		if (c->style->position == LAM_UI_POSITION_ABSOLUTE)
			continue;

		/* always fill available inner width if not explicitly set */
		if (c->w == LAM_UI_AUTO || c->style->width == LAM_UI_AUTO)
			c->w = (uint32_t)inner_w;

		/* Block child height */
		if (container->style->overflow_y == LAM_UI_OVERFLOW_VISIBLE) {
			c->h = c->content_h;
		} else {
			/* overflow != visible, child can't surpass content-box */
			float remaining = inner_y + inner_h - cursor_y;

			if (remaining < 0.f)
				remaining = 0.f;

			uint32_t max_h = (uint32_t)remaining;

			if (c->content_h > max_h)
				c->h = max_h;
			else
				c->h = c->content_h;
		}

		c->x = (int32_t)inner_x;
		c->y = (int32_t)cursor_y;

		cursor_y += (float)c->h;
		if (c->w > used_w)
			used_w = c->w;
	}

	/* absolute-positioned children */
	lam_ui_layout_absolute_children(container, NULL);

	/* recurse into children (block or flex) */
	lam_ui_detect_overflow_and_recurse(container);

	/* update intrinsic size */
	container->content_w = used_w
		+ container->style->padding_left
		+ container->style->padding_right
		+ 2 * container->style->border_width;

	container->content_h = (uint32_t)(cursor_y - inner_y)
		+ container->style->padding_top
		+ container->style->padding_bottom
		+ 2 * container->style->border_width;
}

void lam_ui_layout(lam_ui_div_t *root)
{
	if (!root->parent) {
		root->x = 0;
		root->y = 0;
		root->w = (root->style && root->style->width > 0) ? (uint32_t)root->style->width : g_screen_w;
		root->h = (root->style && root->style->height > 0) ? (uint32_t)root->style->height : g_screen_h;
	}

	/* Use the new flex-based layout entry */
	lam_ui_layout_dispatch(root);
	root->needs_layout = 0;
}

/*
 * render_div_raw — low-level draw: paints the element and children
 * without ever building or drawing from cache.
 */
void render_div_raw(lam_ui_div_t *d)
{
	const lam_ui_style_t *s;

	s = d->style;

	/* skip if hidden, zero size, or subtree invisible */
	if (s->display == LAM_UI_DISPLAY_NONE ||
	    d->w == 0 || d->h == 0 ||
	    (!d->first_child && !d->text && s->background_image_ref == CM_MISS &&
	     !s->background_color && !s->border_width))
		return;

	/* box-shadow */
	if (s->box_shadow_color && s->box_shadow_blur > 0.0f) {
		lam_box_shadow(
				s->box_shadow_color,
				d->x,
				d->y,
				d->w,
				d->h,
				(float)s->border_radius_top_left,
				(float)s->border_radius_top_right,
				(float)s->border_radius_bottom_right,
				(float)s->border_radius_bottom_left,
				s->box_shadow_blur,
				s->box_shadow_offset_x,
				s->box_shadow_offset_y
			      );
	}

	/* background */
	if (s->background_image_ref != CM_MISS)
		lam_tex_draw(s->background_image_ref, d->x, d->y, d->w, d->h);
	else if (s->background_color || s->border_width) {
		int has_radius =
			s->border_radius_top_left |
			s->border_radius_top_right |
			s->border_radius_bottom_right |
			s->border_radius_bottom_left;

		if (has_radius)
			lam_border_radius(
					s->background_color,
					s->border_color,
					d->x, d->y, d->w, d->h,
					(float)s->border_radius_top_left,
					(float)s->border_radius_top_right,
					(float)s->border_radius_bottom_right,
					(float)s->border_radius_bottom_left,
					s->border_width
					);
		else {
			if (s->background_color)
				lam_fill(d->x, d->y, d->w, d->h, s->background_color);

			if (s->border_width) {
				uint32_t b = s->border_width, col = s->border_color;
				lam_fill(d->x, d->y, d->w, b, col);
				lam_fill(d->x, d->y + (int32_t)d->h - (int32_t)b, d->w, b, col);
				lam_fill(d->x, d->y, b, d->h, col);
				lam_fill(d->x + (int32_t)d->w - (int32_t)b, d->y, b, d->h, col);
			}
		}
	}

	/* text */
	if (d->text && s->font_family_ref != CM_MISS) {
		int32_t border_w = s->border_width * 2;
		int32_t border_h = s->border_width * 2;

		int32_t tx = d->x + s->padding_left + s->border_width;
		// int32_t ty = d->y + s->padding_top + s->border_width;
		int32_t tw = d->w - (s->padding_left + s->padding_right) - border_w;
		int32_t th = d->h - (s->padding_top + s->padding_bottom) - border_h;

		if (tw > 0 && th > 0) {
			uint32_t mw = 0, mh = 0;
			lam_font_measure(&mw, &mh,
					s->font_family_ref, d->text,
					0, 0, (uint32_t)tw, (uint32_t)th,
					s->font_size,
					s->white_space,
					s->word_break);

			int32_t align_x = tx;

			switch (s->text_align) {
				case LAM_UI_TEXT_ALIGN_CENTER:
					align_x = tx + ((int32_t)tw - (int32_t)mw) / 2;
					break;
				case LAM_UI_TEXT_ALIGN_RIGHT:
					align_x = tx + ((int32_t)tw - (int32_t)mw);
					break;
				default:
					break;
			}

			lam_tint(s->color ? s->color : lam_default_tint);
			// uint32_t inner_x0 = d->x + s->border_width + s->padding_left;
			uint32_t inner_y0 = d->y + s->border_width + s->padding_top;
			uint32_t inner_x1 = d->x + d->w - (s->border_width + s->padding_right);
			uint32_t inner_y1 = d->y + d->h - (s->border_width + s->padding_bottom);

			lam_font_draw(s->font_family_ref,
					d->text,
					align_x, inner_y0,
					inner_x1, inner_y1,
					s->font_size,
					s->white_space,
					s->word_break);

			lam_tint(lam_default_tint);
		}
	}

	for (lam_ui_div_t *c = d->first_child; c; c = c->next_sibling)
		render_div_raw(c);
}

/*
 * render_div — high-level draw using caching.
 * If cache valid, draws cached texture; otherwise builds it.
 */
extern GLuint g_fbo;

static void render_div(struct lam_ui_div *d)
{
	const lam_ui_style_t *s;

	if (!g_fbo)
		return;

	if (d->style->display == LAM_UI_DISPLAY_NONE || d->w == LAM_UI_AUTO || d->h == LAM_UI_AUTO)
		return;

	/* cheap nodes: no bg, no border, no text and has no children */
	s = d->style;
	if (s->background_image_ref == CM_MISS
			&& !s->background_color &&
			!s->border_width
			&& (!d->text || s->font_family_ref == CM_MISS) &&
			!d->first_child) {
		return;
	}

	if (!lam_cache_valid(d))
		lam_cache_build(d);

	lam_cache_draw(d, d->x, d->y);
}

void lam_ui_render(lam_ui_div_t *root)
{
	if (root->needs_layout)
		lam_ui_layout_dispatch(root);

	render_div(root);
}

void lam_ui_clear(lam_ui_div_t *root)
{
	lam_ui_div_t *c, *n;

	c = root->first_child;
	while (c) {
		n = c->next_sibling;
		lam_ui_clear(c);

		if (c->style_calloc && c->style)
			free(c->style);

		free(c);
		c = n;
	}

	root->first_child = NULL;
	lam_ui_mark_dirty(root);
}

const char *lam_ui_overflow(const lam_ui_div_t *div)
{
	return div ? div->overflow : 0;
}

int32_t lam_ui_get_x(const lam_ui_div_t *div)
{
	return div ? div->x : 0;
}

int32_t lam_ui_get_y(const lam_ui_div_t *div)
{
	return div ? div->y : 0;
}

int32_t lam_ui_get_width(const lam_ui_div_t *div)
{
	return div ? div->w : 0;
}

int32_t lam_ui_get_height(const lam_ui_div_t *div)
{
	return div ? div->h : 0;
}

uint32_t lam_ui_get_content_width(const lam_ui_div_t *div)
{
	return div ? div->content_w : 0;
}

uint32_t lam_ui_get_content_height(const lam_ui_div_t *div)
{
	return div ? div->content_h : 0;
}

const char *lam_ui_get_text(const lam_ui_div_t *div)
{
	return div ? div->text : NULL;
}

const char *lam_ui_get_class(const lam_ui_div_t *div)
{
	return div ? div->class_name : NULL;
}
