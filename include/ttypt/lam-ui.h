#ifndef LAM_UI_H
#define LAM_UI_H

/**
 * @file lam-ui.h
 * @brief Public header for the liblamina UI system.
 *
 * Declares the API for liblamina's CSS-styled layout and rendering engine.
 */

#include "lam.h"
#include <stdint.h>
#include <stddef.h>

#include <ttypt/corm.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Assign a style field on a div.
 */
#define LAM_UI_STYLE(div, field, val) \
	do { \
		lam_ui_style_t dummy; \
		dummy.field = val; \
		lam_ui_style_set(div, offsetof(lam_ui_style_t, field), &dummy.field, sizeof(dummy.field)); \
	} while (0)

/** @defgroup lam_ui_types liblamina UI types
 * @brief Fundamental type definitions.
 * @{
 */

static const int32_t LAM_UI_AUTO = -1;

typedef enum {
	LAM_UI_COLUMN,
	LAM_UI_ROW,
	LAM_UI_COLUMN_REVERSE,
	LAM_UI_ROW_REVERSE,
} lam_ui_flex_direction_t;

typedef enum {
	LAM_UI_POSITION_RELATIVE,
	LAM_UI_POSITION_ABSOLUTE,
	LAM_UI_POSITION_FIXED,
	LAM_UI_POSITION_STATIC,
} lam_ui_position_mode_t;

typedef enum {
	LAM_UI_DISPLAY_BLOCK,
	LAM_UI_DISPLAY_INLINE,
	LAM_UI_DISPLAY_FLEX,
	LAM_UI_DISPLAY_NONE,
	LAM_UI_DISPLAY_INLINE_FLEX,
	LAM_UI_DISPLAY_INLINE_BLOCK,
	LAM_UI_DISPLAY_GRID,
	LAM_UI_DISPLAY_CONTENTS,
} lam_ui_display_mode_t;

typedef enum {
	LAM_UI_ALIGN_STRETCH = 0,
	LAM_UI_ALIGN_FLEX_START,
	LAM_UI_ALIGN_CENTER,
	LAM_UI_ALIGN_FLEX_END,
	LAM_UI_ALIGN_AUTO = -1,
} lam_ui_align_mode_t;

typedef enum {
	LAM_UI_JUSTIFY_FLEX_START = 0,
	LAM_UI_JUSTIFY_CENTER,
	LAM_UI_JUSTIFY_FLEX_END,
	LAM_UI_JUSTIFY_SPACE_BETWEEN,
	LAM_UI_JUSTIFY_SPACE_AROUND,
	LAM_UI_JUSTIFY_SPACE_EVENLY,
} lam_ui_justify_content_mode_t;

typedef enum {
	LAM_UI_TEXT_ALIGN_LEFT = 0,
	LAM_UI_TEXT_ALIGN_CENTER,
	LAM_UI_TEXT_ALIGN_RIGHT
} lam_ui_text_align_t;

typedef enum {
	LAM_UI_WS_NORMAL = 0,
	LAM_UI_WS_NOWRAP,
	LAM_UI_WS_PRE,
	LAM_UI_WS_PRE_WRAP,
	LAM_UI_WS_PRE_LINE
} lam_ui_white_space_t;

typedef enum {
	LAM_UI_WB_NORMAL = 0,
	LAM_UI_WB_BREAK_ALL,
	LAM_UI_WB_KEEP_ALL,
	LAM_UI_WB_BREAK_WORD
} lam_ui_word_break_t;

typedef enum {
	LAM_UI_OVERFLOW_HIDDEN = 0,
	LAM_UI_OVERFLOW_VISIBLE,
	LAM_UI_OVERFLOW_AUTO,
	LAM_UI_OVERFLOW_SCROLL,
} lam_ui_overflow_t;

typedef enum {
	LAM_UI_WRAP_NOWRAP = 0,
	LAM_UI_WRAP_WRAP,
} lam_ui_flex_wrap_t;

typedef enum {
	LAM_UI_VIS_VISIBLE = 0,
	LAM_UI_VIS_HIDDEN,
} lam_ui_visibility_t;

typedef enum {
	LAM_UI_PE_AUTO = 0,
	LAM_UI_PE_NONE,
} lam_ui_pointer_events_t;

typedef enum {
	LAM_UI_BOX_BORDER = 0,
	LAM_UI_BOX_CONTENT,
} lam_ui_box_sizing_t;

typedef enum {
	LAM_UI_FIT_FILL = 0,
	LAM_UI_FIT_COVER,
	LAM_UI_FIT_CONTAIN,
} lam_ui_object_fit_t;

typedef enum {
	LAM_LEN_AUTO = 0,
	LAM_LEN_PX,
	LAM_LEN_PCT,
	LAM_LEN_VW,
	LAM_LEN_VH,
	LAM_LEN_EM,
	LAM_LEN_REM,
	LAM_LEN_CALC,
} lam_len_unit_t;

typedef struct {
	lam_len_unit_t unit;
	float          val;
	lam_len_unit_t calc_unit;
	float          calc_px;
} lam_len_t;

typedef enum {
	LAM_PSEUDO_HOVER    = (1u << 0),
	LAM_PSEUDO_ACTIVE   = (1u << 1),
	LAM_PSEUDO_FOCUS    = (1u << 2),
	LAM_PSEUDO_DISABLED = (1u << 3),
	LAM_PSEUDO_CHECKED  = (1u << 4),
} lam_ui_pseudo_t;

/**
 * @brief Visual and layout style definition for a UI element.
 */
typedef struct {
	uint32_t background_color;
	uint32_t border_color;
	uint32_t border_width;
	uint32_t background_image_ref;
	uint32_t font_family_ref;
	uint32_t font_size;
	uint32_t color;

	uint32_t padding_left;
	uint32_t padding_right;
	uint32_t padding_top;
	uint32_t padding_bottom;

	/* positioning */
	lam_ui_position_mode_t position;
	int32_t  top;
	int32_t  left;
	int32_t  right;
	int32_t  bottom;

	lam_ui_display_mode_t  display;

	/* flexbox layout */
	lam_ui_flex_direction_t flex_direction;
	float flex_grow;
	float flex_shrink;
	int32_t flex_basis;

	/* flex alignment properties */
	lam_ui_align_mode_t   align_items;
	lam_ui_align_mode_t   align_self;
	lam_ui_justify_content_mode_t justify_content;

	uint32_t border_radius_top_left;
	uint32_t border_radius_top_right;
	uint32_t border_radius_bottom_right;
	uint32_t border_radius_bottom_left;

	uint32_t box_shadow_color;
	float    box_shadow_blur;
	float    box_shadow_offset_x;
	float    box_shadow_offset_y;

	lam_ui_text_align_t text_align;
	lam_ui_white_space_t white_space;
	lam_ui_word_break_t  word_break;

	int32_t width, height;

	lam_ui_overflow_t overflow_y;

	/* ── Extended CSS layout & visual properties ── */
	lam_len_t width_len;
	lam_len_t height_len;
	lam_len_t min_width;
	lam_len_t max_width;
	lam_len_t min_height;
	lam_len_t max_height;
	lam_len_t top_len;
	lam_len_t left_len;
	lam_len_t right_len;
	lam_len_t bottom_len;
	lam_len_t flex_basis_len;

	int32_t margin_top;
	int32_t margin_right;
	int32_t margin_bottom;
	int32_t margin_left;
	uint8_t margin_top_auto;
	uint8_t margin_right_auto;
	uint8_t margin_bottom_auto;
	uint8_t margin_left_auto;

	uint32_t row_gap;
	uint32_t column_gap;
	lam_ui_flex_wrap_t flex_wrap;

	int32_t z_index;
	uint8_t z_index_set;

	float opacity;
	lam_ui_visibility_t visibility;
	lam_ui_pointer_events_t pointer_events;
	lam_ui_overflow_t overflow_x;

	float transform_tx_px;
	float transform_tx_pct;
	float transform_ty_px;
	float transform_ty_pct;

	uint32_t grid_cols;
	uint32_t grid_col_px;
	uint32_t grid_row_px;

	uint32_t background_color2;
	lam_ui_box_sizing_t box_sizing;
	lam_ui_object_fit_t object_fit;

	uint16_t font_weight;
	uint8_t  font_mono;
	uint8_t  font_size_is_px;
	float    line_height;

	uint32_t border_top_width;
	uint32_t border_right_width;
	uint32_t border_bottom_width;
	uint32_t border_left_width;
	uint32_t border_top_color;
	uint32_t border_right_color;
	uint32_t border_bottom_color;
	uint32_t border_left_color;

	uint8_t  border_radius_pct; /* 1 if 50% / pill */

	uint8_t  bg_grad_type;      /* 0=none, 1=linear, 2=radial */
	float    bg_grad_angle;     /* CSS degrees (default 180 = to bottom) */
	uint8_t  bg_size_mode;      /* 0=stretch, 1=cover, 2=contain */
	uint8_t  bg_blend_mode;     /* 0=normal, 1=multiply, 2=screen */

	uint32_t text_shadow_color;
	int8_t   text_shadow_x;
	int8_t   text_shadow_y;

	uint32_t outline_color;
	uint8_t  outline_width;
	uint8_t  cursor;            /* lam_cursor_t (0 = auto) */
	float    backdrop_blur;

	uint64_t set_mask[2];
	uint64_t imp_mask[2];
} lam_ui_style_t;

typedef struct lam_ui_div lam_ui_div_t;

typedef void (*lam_ui_custom_draw_fn)(lam_ui_div_t *div,
                                      int32_t x, int32_t y,
                                      int32_t w, int32_t h,
                                      void *user);

/** @} */

/** @defgroup lam_ui_core liblamina UI initialization and stylesheet
 * @{
 */

void lam_ui_init(uint32_t screen_w, uint32_t screen_h);
void lam_ui_resize(uint32_t screen_w, uint32_t screen_h);
void lam_ui_set_fonts(uint32_t sans, uint32_t sans_bold,
                      uint32_t mono, uint32_t mono_bold);
void lam_ui_set_dark_mode(int dark);
void lam_ui_set_asset_root(const char *dir);

uint32_t lam_ui_stylesheet_init(void);

static inline void lam_ui_stylesheet_add(uint32_t ss,
			const char *class_name,
			const lam_ui_style_t *style)
{
	corm_put(ss, class_name, style);
}

/**
 * @brief Parse CSS text and append rules/variables into stylesheet handle ss.
 * @return Number of rules parsed, or -1 on error.
 */
int lam_css_parse(uint32_t ss, const char *css, size_t len);

/**
 * @brief Load a CSS file from disk and parse it into stylesheet handle ss.
 * @return Number of rules parsed, or -1 on error.
 */
int lam_css_load_file(uint32_t ss, const char *path);

/**
 * @brief Free CSS rules and custom properties associated with stylesheet ss.
 */
void lam_css_sheet_free(uint32_t ss);

/** @} */

/** @defgroup lam_ui_tree liblamina UI element tree
 * @{
 */

lam_ui_div_t *lam_ui_new(lam_ui_div_t *parent, lam_ui_style_t *style);
lam_ui_div_t *lam_ui_flex(lam_ui_div_t *parent, lam_ui_flex_direction_t dir);
lam_ui_div_t *lam_ui_box(lam_ui_div_t *parent, int32_t w, int32_t h, uint32_t bg_color);
lam_ui_div_t *lam_ui_label(lam_ui_div_t *parent, const char *text, uint32_t font_ref, uint32_t color);

void lam_ui_set_size(lam_ui_div_t *div, int32_t w, int32_t h);
void lam_ui_set_bg(lam_ui_div_t *div, uint32_t color);
void lam_ui_set_padding(lam_ui_div_t *div, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom);
void lam_ui_set_flex(lam_ui_div_t *div, float grow, float shrink, int32_t basis);
void lam_ui_set_align(lam_ui_div_t *div, lam_ui_align_mode_t align_items, lam_ui_justify_content_mode_t justify_content);

void lam_ui_class(lam_ui_div_t *div, const char *class_name);
void lam_ui_text(lam_ui_div_t *div, const char *text);
void lam_ui_set_tag(lam_ui_div_t *div, const char *tag);
void lam_ui_set_id(lam_ui_div_t *div, const char *id);
void lam_ui_set_attr(lam_ui_div_t *div, const char *name, const char *val);
void lam_ui_remove_attr(lam_ui_div_t *div, const char *name);
void lam_ui_insert_before(lam_ui_div_t *parent, lam_ui_div_t *child, lam_ui_div_t *before);
void lam_ui_remove_child(lam_ui_div_t *parent, lam_ui_div_t *child);
void lam_ui_set_inline_style(lam_ui_div_t *div, const char *css_decls);
void lam_ui_set_pseudo(lam_ui_div_t *div, uint32_t pseudo_mask);
uint32_t lam_ui_get_pseudo(const lam_ui_div_t *div);
void lam_ui_set_scroll(lam_ui_div_t *div, int32_t sx, int32_t sy);
void lam_ui_get_scroll(const lam_ui_div_t *div, int32_t *sx, int32_t *sy);
void lam_ui_set_custom_draw(lam_ui_div_t *div, lam_ui_custom_draw_fn fn, void *user);
void lam_ui_set_user_data(lam_ui_div_t *div, void *user);
void *lam_ui_get_user_data(const lam_ui_div_t *div);

const char *lam_ui_overflow(const lam_ui_div_t *div);

/** @} */

/** @defgroup lam_ui_layout liblamina UI layout and rendering
 * @{
 */

void lam_ui_apply_styles(lam_ui_div_t *root, uint32_t ss);
void lam_ui_layout(lam_ui_div_t *root);
void lam_ui_render(lam_ui_div_t *root);
void lam_ui_clear(lam_ui_div_t *root);
void lam_ui_destroy(lam_ui_div_t *root);
void lam_ui_style_reset(lam_ui_style_t *s);

void lam_border_radius(uint32_t background_color,
                       uint32_t border_color,
                       int32_t x, int32_t y,
                       uint32_t w, uint32_t h,
                       float tl, float tr,
                       float br, float bl,
                       float border_width);

void lam_border_radius_grad(uint32_t background_color,
                            uint32_t background_color2,
                            uint8_t grad_type,
                            float grad_angle_deg,
                            uint32_t border_color,
                            int32_t x, int32_t y,
                            uint32_t w, uint32_t h,
                            float tl, float tr,
                            float br, float bl,
                            float border_width);

void lam_tex_draw_box(uint32_t tex_ref,
                      int32_t x, int32_t y,
                      uint32_t w, uint32_t h,
                      float tl, float tr,
                      float br, float bl,
                      uint8_t fit_mode,
                      uint8_t blend_mode,
                      uint32_t bg_tint,
                      float opacity);

void lam_box_shadow(uint32_t color,
		    int32_t x, int32_t y,
		    uint32_t w, uint32_t h,
		    float tl, float tr, float br, float bl,
		    float blur_radius, float offset_x, float offset_y);

void lam_ui_mark_dirty(lam_ui_div_t *d);
void lam_ui_style_set(lam_ui_div_t *root, size_t offset,
		      const void *value, size_t size);

int32_t  lam_ui_get_x(const lam_ui_div_t *div);
int32_t  lam_ui_get_y(const lam_ui_div_t *div);
int32_t  lam_ui_get_width(const lam_ui_div_t *div);
int32_t  lam_ui_get_height(const lam_ui_div_t *div);
uint32_t lam_ui_get_content_width(const lam_ui_div_t *div);
uint32_t lam_ui_get_content_height(const lam_ui_div_t *div);
const char *lam_ui_get_text(const lam_ui_div_t *div);
const char *lam_ui_get_class(const lam_ui_div_t *div);
const char *lam_ui_get_tag(const lam_ui_div_t *div);
const char *lam_ui_get_id(const lam_ui_div_t *div);
const char *lam_ui_get_attr(const lam_ui_div_t *div, const char *name);
const lam_ui_style_t *lam_ui_get_style(const lam_ui_div_t *div);
lam_ui_div_t *lam_ui_parent(const lam_ui_div_t *div);
lam_ui_div_t *lam_ui_first_child(const lam_ui_div_t *div);
lam_ui_div_t *lam_ui_next_sibling(const lam_ui_div_t *div);

lam_ui_div_t *lam_ui_hit_test(lam_ui_div_t *root, int32_t px, int32_t py);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LAM_UI_H */
