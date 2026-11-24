#include "./ui.h"
#include "../include/ttypt/lam-font.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <ttypt/corm.h>

/* stylesheet cache for fast legacy class lookup */
static lam_ui_style_t lam_style_default;
static uint32_t cm_style;

int      lam_ui_dark_mode      = 1;
uint32_t lam_ui_font_sans      = CM_MISS;
uint32_t lam_ui_font_sans_bold = CM_MISS;
uint32_t lam_ui_font_mono      = CM_MISS;
uint32_t lam_ui_font_mono_bold = CM_MISS;
char     lam_ui_asset_root[512] = "";

void lam_ui_set_dark_mode(int dark)
{
	lam_ui_dark_mode = dark ? 1 : 0;
}

void lam_ui_set_asset_root(const char *dir)
{
	if (!dir || !*dir) {
		lam_ui_asset_root[0] = '\0';
		return;
	}
	size_t n = strlen(dir);
	if (n >= sizeof(lam_ui_asset_root))
		n = sizeof(lam_ui_asset_root) - 1;
	memcpy(lam_ui_asset_root, dir, n);
	lam_ui_asset_root[n] = '\0';
	while (n > 1 && lam_ui_asset_root[n - 1] == '/')
		lam_ui_asset_root[--n] = '\0';
}

void lam_ui_set_fonts(uint32_t sans, uint32_t sans_bold,
                      uint32_t mono, uint32_t mono_bold)
{
	lam_ui_font_sans      = sans;
	lam_ui_font_sans_bold = (sans_bold != CM_MISS) ? sans_bold : sans;
	lam_ui_font_mono      = (mono != CM_MISS)      ? mono      : sans;
	lam_ui_font_mono_bold = (mono_bold != CM_MISS) ? mono_bold : lam_ui_font_mono;
}

void style_init(void)
{
	if (!cm_style)
		cm_style = corm_reg(sizeof(lam_ui_style_t));
	lam_ui_style_reset(&lam_style_default);
}

uint32_t lam_ui_stylesheet_init(void)
{
	if (!cm_style)
		style_init();
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
	s->background_color2 = 0;
	s->background_image_ref = CM_MISS;
	s->flex_grow = 0;
	s->flex_shrink = 1;
	s->flex_basis = LAM_UI_AUTO;
	s->flex_direction = LAM_UI_ROW;

	s->border_radius_top_left =
		s->border_radius_top_right =
		s->border_radius_bottom_right =
		s->border_radius_bottom_left = 0;

	s->box_shadow_color = 0;
	s->box_shadow_blur = 0.0f;
	s->box_shadow_offset_x = 0.0f;
	s->box_shadow_offset_y = 0.0f;

	s->text_align = LAM_UI_TEXT_ALIGN_LEFT;
	s->white_space = LAM_UI_WS_NORMAL;
	s->word_break = LAM_UI_WB_NORMAL;
	s->overflow_x = LAM_UI_OVERFLOW_VISIBLE;
	s->overflow_y = LAM_UI_OVERFLOW_VISIBLE;

	s->width = s->height = LAM_UI_AUTO;

	memset(&s->width_len, 0, sizeof(s->width_len));
	memset(&s->height_len, 0, sizeof(s->height_len));
	memset(&s->min_width, 0, sizeof(s->min_width));
	memset(&s->max_width, 0, sizeof(s->max_width));
	memset(&s->min_height, 0, sizeof(s->min_height));
	memset(&s->max_height, 0, sizeof(s->max_height));
	memset(&s->top_len, 0, sizeof(s->top_len));
	memset(&s->left_len, 0, sizeof(s->left_len));
	memset(&s->right_len, 0, sizeof(s->right_len));
	memset(&s->bottom_len, 0, sizeof(s->bottom_len));
	memset(&s->flex_basis_len, 0, sizeof(s->flex_basis_len));

	s->margin_top = s->margin_right = s->margin_bottom = s->margin_left = 0;
	s->margin_top_auto = s->margin_right_auto =
		s->margin_bottom_auto = s->margin_left_auto = 0;

	s->row_gap = s->column_gap = 0;
	s->flex_wrap = LAM_UI_WRAP_NOWRAP;
	s->z_index = 0;
	s->z_index_set = 0;
	s->opacity = 1.0f;
	s->visibility = LAM_UI_VIS_VISIBLE;
	s->pointer_events = LAM_UI_PE_AUTO;

	s->transform_tx_px = s->transform_tx_pct = 0.0f;
	s->transform_ty_px = s->transform_ty_pct = 0.0f;

	s->grid_cols = s->grid_col_px = s->grid_row_px = 0;
	s->box_sizing = LAM_UI_BOX_BORDER;
	s->object_fit = LAM_UI_FIT_FILL;
	s->font_weight = 400;
	s->font_mono = 0;
	s->font_size_is_px = 0;
	s->line_height = 0.0f;

	s->border_top_width = s->border_right_width =
		s->border_bottom_width = s->border_left_width = 0;
	s->border_top_color = s->border_right_color =
		s->border_bottom_color = s->border_left_color = 0;
	s->border_radius_pct = 0;

	s->bg_grad_type = 0;
	s->bg_grad_angle = 180.0f;
	s->bg_size_mode = 0;
	s->bg_blend_mode = 0;
	s->text_shadow_color = 0;
	s->text_shadow_x = 0;
	s->text_shadow_y = 0;
	s->outline_color = 0;
	s->outline_width = 0;
	s->cursor = LAM_CURSOR_AUTO;
	s->backdrop_blur = 0.0f;

	s->set_mask[0] = s->set_mask[1] = 0;
	s->imp_mask[0] = s->imp_mask[1] = 0;
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
	MERGE(background_color2);
	MERGE(border_color);
	MERGE(border_width);
	MERGE(background_image_ref);
	MERGE(font_family_ref);
	MERGE(font_size);
	MERGE(font_size_is_px);
	MERGE(font_weight);
	MERGE(font_mono);
	MERGE(line_height);
	MERGE(color);

	MERGE(align_items);
	MERGE(justify_content);
	MERGE(align_self);

	MERGE(padding_left);
	MERGE(padding_right);
	MERGE(padding_top);
	MERGE(padding_bottom);

	MERGE(margin_left);
	MERGE(margin_right);
	MERGE(margin_top);
	MERGE(margin_bottom);
	MERGE(margin_left_auto);
	MERGE(margin_right_auto);
	MERGE(margin_top_auto);
	MERGE(margin_bottom_auto);

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
	MERGE(flex_wrap);
	MERGE(row_gap);
	MERGE(column_gap);

	MERGE(border_radius_top_left);
	MERGE(border_radius_top_right);
	MERGE(border_radius_bottom_right);
	MERGE(border_radius_bottom_left);
	MERGE(border_radius_pct);

	MERGE(box_shadow_color);
	MERGE(box_shadow_blur);
	MERGE(box_shadow_offset_x);
	MERGE(box_shadow_offset_y);

	MERGE(text_align);
	MERGE(white_space);
	MERGE(word_break);

	MERGE(width);
	MERGE(height);
	MERGE(overflow_x);
	MERGE(overflow_y);
	MERGE(z_index);
	MERGE(z_index_set);
	MERGE(opacity);
	MERGE(visibility);
	MERGE(pointer_events);
	MERGE(box_sizing);
	MERGE(object_fit);

	MERGE(grid_cols);
	MERGE(grid_col_px);
	MERGE(grid_row_px);

	MERGE(border_top_width);
	MERGE(border_right_width);
	MERGE(border_bottom_width);
	MERGE(border_left_width);
	MERGE(border_top_color);
	MERGE(border_right_color);
	MERGE(border_bottom_color);
	MERGE(border_left_color);

	MERGE(bg_grad_type);
	MERGE(bg_grad_angle);
	MERGE(bg_size_mode);
	MERGE(bg_blend_mode);
	MERGE(text_shadow_color);
	MERGE(text_shadow_x);
	MERGE(text_shadow_y);
	MERGE(outline_color);
	MERGE(outline_width);
	MERGE(cursor);
	MERGE(backdrop_blur);

	if (src->width_len.unit != LAM_LEN_AUTO) dst->width_len = src->width_len;
	if (src->height_len.unit != LAM_LEN_AUTO) dst->height_len = src->height_len;
	if (src->min_width.unit != LAM_LEN_AUTO) dst->min_width = src->min_width;
	if (src->max_width.unit != LAM_LEN_AUTO) dst->max_width = src->max_width;
	if (src->min_height.unit != LAM_LEN_AUTO) dst->min_height = src->min_height;
	if (src->max_height.unit != LAM_LEN_AUTO) dst->max_height = src->max_height;
	if (src->top_len.unit != LAM_LEN_AUTO) dst->top_len = src->top_len;
	if (src->left_len.unit != LAM_LEN_AUTO) dst->left_len = src->left_len;
	if (src->right_len.unit != LAM_LEN_AUTO) dst->right_len = src->right_len;
	if (src->bottom_len.unit != LAM_LEN_AUTO) dst->bottom_len = src->bottom_len;
	if (src->flex_basis_len.unit != LAM_LEN_AUTO) dst->flex_basis_len = src->flex_basis_len;
#undef MERGE
}

static void apply_legacy_corm_classes(lam_ui_style_t *dst, uint32_t ss, const char *class_name)
{
	if (!ss || !class_name || !*class_name)
		return;

	/* Exact whole-string match first (legacy single class) */
	const lam_ui_style_t *exact = (const lam_ui_style_t *)corm_get(ss, class_name);
	if (exact) {
		lam_ui_style_merge(dst, exact);
		return;
	}

	/* Space-separated classes */
	const char *p = class_name;
	while (*p) {
		while (*p && isspace((unsigned char)*p)) p++;
		if (!*p) break;
		const char *s = p;
		while (*p && !isspace((unsigned char)*p)) p++;
		size_t l = (size_t)(p - s);
		if (l > 0 && l < 128) {
			char buf[128];
			memcpy(buf, s, l);
			buf[l] = '\0';
			const lam_ui_style_t *st = (const lam_ui_style_t *)corm_get(ss, buf);
			if (st)
				lam_ui_style_merge(dst, st);
		}
	}
}

static int style_layout_diff(const lam_ui_style_t *a, const lam_ui_style_t *b)
{
	return
		a->padding_left   != b->padding_left   ||
		a->padding_right  != b->padding_right  ||
		a->padding_top    != b->padding_top    ||
		a->padding_bottom != b->padding_bottom ||
		a->margin_left    != b->margin_left    ||
		a->margin_right   != b->margin_right   ||
		a->margin_top     != b->margin_top     ||
		a->margin_bottom  != b->margin_bottom  ||
		a->border_width   != b->border_width   ||
		a->flex_grow      != b->flex_grow      ||
		a->flex_shrink    != b->flex_shrink    ||
		a->flex_basis     != b->flex_basis     ||
		a->flex_direction != b->flex_direction ||
		a->flex_wrap      != b->flex_wrap      ||
		a->row_gap        != b->row_gap        ||
		a->column_gap     != b->column_gap     ||
		a->align_items    != b->align_items    ||
		a->align_self     != b->align_self     ||
		a->justify_content!= b->justify_content||
		a->display        != b->display        ||
		a->position       != b->position       ||
		a->left           != b->left           ||
		a->right          != b->right          ||
		a->top            != b->top            ||
		a->bottom         != b->bottom         ||
		a->width          != b->width          ||
		a->height         != b->height         ||
		a->width_len.unit != b->width_len.unit ||
		a->width_len.val  != b->width_len.val  ||
		a->height_len.unit!= b->height_len.unit||
		a->height_len.val != b->height_len.val ||
		a->font_family_ref!= b->font_family_ref||
		a->font_size      != b->font_size      ||
		a->white_space    != b->white_space    ||
		a->word_break     != b->word_break;
}

static void init_inherited(lam_ui_style_t *dst, const lam_ui_style_t *parent_style)
{
	lam_ui_style_reset(dst);
	if (!parent_style)
		return;
	/* Real CSS inherited properties */
	dst->color             = parent_style->color;
	dst->font_family_ref   = parent_style->font_family_ref;
	dst->font_size         = parent_style->font_size;
	dst->font_size_is_px   = parent_style->font_size_is_px;
	dst->font_weight       = parent_style->font_weight;
	dst->font_mono         = parent_style->font_mono;
	dst->line_height       = parent_style->line_height;
	dst->text_align        = parent_style->text_align;
	dst->white_space       = parent_style->white_space;
	dst->word_break        = parent_style->word_break;
	dst->visibility        = parent_style->visibility;
	dst->pointer_events    = parent_style->pointer_events;
	dst->cursor            = parent_style->cursor;
	dst->text_shadow_color = parent_style->text_shadow_color;
	dst->text_shadow_x     = parent_style->text_shadow_x;
	dst->text_shadow_y     = parent_style->text_shadow_y;
}

static void resolve_default_font(lam_ui_style_t *st)
{
	if (st->font_family_ref == CM_MISS && lam_ui_font_sans != CM_MISS) {
		int bold = (st->font_weight >= 600);
		if (st->font_mono)
			st->font_family_ref = bold ? lam_ui_font_mono_bold : lam_ui_font_mono;
		else
			st->font_family_ref = bold ? lam_ui_font_sans_bold : lam_ui_font_sans;
		if (!st->font_size_is_px) {
			st->font_size = 14;
			st->font_size_is_px = 1;
		}
	} else if (st->font_family_ref != CM_MISS && lam_ui_font_sans != CM_MISS) {
		/* If font was inherited from registered default fonts and mono/weight changed */
		int is_default_font =
			st->font_family_ref == lam_ui_font_sans ||
			st->font_family_ref == lam_ui_font_sans_bold ||
			st->font_family_ref == lam_ui_font_mono ||
			st->font_family_ref == lam_ui_font_mono_bold;
		if (is_default_font) {
			int bold = (st->font_weight >= 600);
			if (st->font_mono)
				st->font_family_ref = bold ? lam_ui_font_mono_bold : lam_ui_font_mono;
			else
				st->font_family_ref = bold ? lam_ui_font_sans_bold : lam_ui_font_sans;
		}
	}
}

static void apply_style_recursive(lam_ui_div_t *div,
		uint32_t ss,
		const lam_ui_style_t *parent_style)
{
	lam_ui_style_t merged;
	lam_ui_style_t old = *div->style;
	lam_ui_div_t *c;

	init_inherited(&merged, parent_style);

	/* 1. Parsed CSS stylesheet cascade */
	lam_css_apply_node(div, ss, &merged);

	/* 2. Legacy corm class rules */
	apply_legacy_corm_classes(&merged, ss, div->class_name);

	/* 3. Programmatic inline overrides (LAM_UI_STYLE / lam_ui_set_*) */
	if (div->inline_override)
		lam_ui_style_merge(&merged, div->inline_override);

	/* 4. Inline CSS style="..." attribute (highest author specificity) */
	if (div->inline_style && *div->inline_style)
		lam_css_apply_inline_str(&merged, ss, div->inline_style);

	/* 5. Resolve default sans/mono TTF font handles if registered */
	resolve_default_font(&merged);

	*div->style = merged;

	if (style_layout_diff(&old, &merged))
		lam_ui_mark_dirty(div);

	for (c = div->first_child; c; c = c->next_sibling)
		apply_style_recursive(c, ss, div->style);
}

void lam_ui_apply_styles(lam_ui_div_t *root, uint32_t ss)
{
	if (!root)
		return;
	if (!cm_style)
		style_init();
	apply_style_recursive(root, ss, NULL);
}

void lam_ui_style_set(lam_ui_div_t *root, size_t offset,
		const void *value, size_t size)
{
	if (!root || !root->style)
		return;

	if (!root->inline_override) {
		root->inline_override = calloc(1, sizeof(lam_ui_style_t));
		if (root->inline_override)
			lam_ui_style_reset(root->inline_override);
	}
	if (root->inline_override)
		memcpy((char *)root->inline_override + offset, value, size);

	if (!memcmp((char *)root->style + offset, value, size))
		return;

	memcpy((char *)root->style + offset, value, size);
	lam_ui_mark_dirty(root);
}
