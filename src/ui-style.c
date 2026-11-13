#include "./ui.h"

#include <string.h>
#include <ttypt/corm.h>

/* stylesheet cache for fast class lookup */
static lam_ui_style_t lam_style_default;
static uint32_t cm_style;

uint32_t lam_ui_stylesheet_init(void)
{
	// CM_PTR seems broken
	return corm_open(
			NULL, NULL,
			CM_STR, cm_style,
			0x1FF, 0);
}

void lam_ui_style_default(lam_ui_style_t *s)
{
	s->font_size = 1;
	s->font_family_ref = CM_MISS;
	s->display = LAM_UI_DISPLAY_BLOCK;

	s->align_items = LAM_UI_ALIGN_STRETCH;
	s->justify_content = LAM_UI_JUSTIFY_FLEX_START;
	s->align_self = LAM_UI_ALIGN_AUTO;

	s->position = LAM_UI_POSITION_RELATIVE;
	s->left = s->right = LAM_UI_AUTO;
	s->top  = s->bottom = LAM_UI_AUTO;

	s->padding_top = s->padding_bottom = 0;
	s->padding_left = s->padding_right = 0;
	s->border_color = 0;
	s->border_width = 0;
	s->background_color = 0;
	s->background_image_ref = CM_MISS;
	s->flex_grow = 0;
	s->flex_shrink = 1;
	s->flex_basis = LAM_UI_AUTO;
	s->flex_direction = LAM_UI_COLUMN;

	s->border_radius_top_left =
		s->border_radius_top_right =
		s->border_radius_bottom_right =
		s->border_radius_bottom_left = 0;

	s->box_shadow_color = 0;
	s->box_shadow_blur = 0.0f;
	s->box_shadow_offset_x = 0.0f;
	s->box_shadow_offset_y = 0.0f;

	s->text_align = LAM_UI_TEXT_ALIGN_LEFT;
	s->overflow_y = LAM_UI_OVERFLOW_HIDDEN;

	s->width = s->height = LAM_UI_AUTO;
}

void lam_ui_style_reset(lam_ui_style_t *s)
{
	memset(s, 0, sizeof(*s));
	lam_ui_style_default(s);
}

static void lam_ui_style_merge(
		lam_ui_style_t *dst, const lam_ui_style_t *src)
{
	if (!src)
		return;

#define MERGE(f) do { \
	if (src->f != lam_style_default.f) \
	dst->f = src->f; \
} while (0)

	MERGE(background_color);
	MERGE(border_color);
	MERGE(border_width);
	MERGE(background_image_ref);
	MERGE(font_family_ref);
	MERGE(font_size);
	MERGE(color);

	MERGE(align_items);
	MERGE(justify_content);
	MERGE(align_self);

	MERGE(padding_left);
	MERGE(padding_right);
	MERGE(padding_top);
	MERGE(padding_bottom);

	MERGE(position);
	MERGE(left);
	MERGE(right);
	MERGE(top);
	MERGE(bottom);

	MERGE(display);
	MERGE(flex_grow);
	MERGE(flex_basis);
	MERGE(flex_shrink);
	MERGE(flex_direction);

	MERGE(border_radius_top_left);
	MERGE(border_radius_top_right);
	MERGE(border_radius_bottom_right);
	MERGE(border_radius_bottom_left);

	MERGE(box_shadow_color);
	MERGE(box_shadow_blur);
	MERGE(box_shadow_offset_x);
	MERGE(box_shadow_offset_y);

	MERGE(text_align);
	MERGE(white_space);
	MERGE(word_break);

	MERGE(width);
	MERGE(height);

	MERGE(overflow_y);
#undef MERGE
}

void style_init(void) {
	cm_style = corm_reg(sizeof(lam_ui_style_t));
	lam_ui_style_reset(&lam_style_default);
}

static inline const lam_ui_style_t *lam_ui_stylesheet_lookup_fast(
		uint32_t ss, const char *class_name)
{
	if (!class_name)
		return NULL;
	return (const lam_ui_style_t *)
		corm_get(ss, class_name);
}

/* fast path using corm handle */
static inline const lam_ui_style_t *lam_ui_stylesheet_lookup(
		uint32_t ss,
		const char *class_name)
{
	return lam_ui_stylesheet_lookup_fast(ss, class_name);
}

/* Returns 1 if any layout-affecting field changed. */
static int style_layout_diff(const lam_ui_style_t *a, const lam_ui_style_t *b)
{
	return
		a->padding_left   != b->padding_left   ||
		a->padding_right  != b->padding_right  ||
		a->padding_top    != b->padding_top    ||
		a->padding_bottom != b->padding_bottom ||
		a->border_width   != b->border_width   ||
		a->flex_grow      != b->flex_grow      ||
		a->flex_shrink    != b->flex_shrink    ||
		a->flex_basis     != b->flex_basis     ||
		a->flex_direction != b->flex_direction ||
		a->align_items    != b->align_items    ||
		a->align_self     != b->align_self     ||
		a->justify_content!= b->justify_content||
		a->display        != b->display        ||
		a->position       != b->position       ||
		a->left           != b->left           ||
		a->right          != b->right          ||
		a->top            != b->top            ||
		a->bottom         != b->bottom         ||
		/* text metrics */
		a->font_family_ref!= b->font_family_ref||
		a->font_size      != b->font_size      ||
		a->white_space    != b->white_space    ||
		a->word_break     != b->word_break;
}

static void apply_style_recursive(lam_ui_div_t *div,
		uint32_t ss,
		const lam_ui_style_t *inherited)
{
	const lam_ui_style_t *class_style;
	lam_ui_style_t merged = *inherited;
	lam_ui_style_t old = *div->style;
	lam_ui_div_t *c;

	class_style = lam_ui_stylesheet_lookup(ss,
			div->class_name);

	if (class_style)
		lam_ui_style_merge(&merged, class_style);

	lam_ui_style_merge(&merged, div->style);

	*div->style = merged;

	if (style_layout_diff(&old, &merged))
		lam_ui_mark_dirty(div);

	/* inheritance */
	lam_ui_style_t child_inh = *div->style;
	lam_ui_style_default(&child_inh);

	for (c = div->first_child; c; c = c->next_sibling)
		apply_style_recursive(c, ss, &child_inh);
}

void lam_ui_apply_styles(lam_ui_div_t *root, uint32_t ss)
{
	lam_ui_style_t base;

	lam_ui_style_reset(&base);
	apply_style_recursive(root, ss, &base);
}

void lam_ui_style_set(lam_ui_div_t *root, size_t offset,
		const void *value, size_t size)
{
	if (!memcmp((char *) root->style + offset, value, size))
		return;

	memcpy((char *) root->style + offset, value, size);
	lam_ui_mark_dirty(root);
}
