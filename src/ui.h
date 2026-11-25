#ifndef LAM_UI_INTERNAL_H
#define LAM_UI_INTERNAL_H

#include "../include/ttypt/lam-ui.h"
#include <stdint.h>

typedef struct {
	char *name;
	char *val;
} lam_ui_attr_t;

struct lam_ui_div {
	struct lam_ui_div *parent;
	struct lam_ui_div *first_child;
	struct lam_ui_div *next_sibling;
	struct lam_ui_div *last_child;

	int32_t x, y, w, h;
	uint32_t content_w, content_h;
	uint32_t intrinsic_w, intrinsic_h;

	const char *class_name;
	char       *class_owned;
	const char *text;
	char       *text_owned;

	char       *tag;
	char       *id;
	char       *inline_style;
	lam_ui_attr_t *attrs;
	uint16_t    n_attrs;
	uint16_t    cap_attrs;
	uint32_t    pseudo_mask;

	int32_t     scroll_x;
	int32_t     scroll_y;

	lam_ui_custom_draw_fn custom_draw;
	void       *custom_draw_user;
	void       *user_data;

	lam_ui_style_t *style;
	lam_ui_style_t *inline_override;
	const char *overflow;

	int style_calloc;
	void *_cache;

	int needs_layout;

	uint32_t measured_w, measured_h;
	uint32_t measured_cw, measured_ch;
};

/* Global UI state shared across ui*.c */
extern uint32_t lam_ui_screen_w;
extern uint32_t lam_ui_screen_h;
extern int      lam_ui_dark_mode;
extern uint32_t lam_ui_font_sans;
extern uint32_t lam_ui_font_sans_bold;
extern uint32_t lam_ui_font_mono;
extern uint32_t lam_ui_font_mono_bold;
extern char     lam_ui_asset_root[512];

void lam_ui_style_default(lam_ui_style_t *s);
void render_div_raw(lam_ui_div_t *d);
void lam_cache_free_div(lam_ui_div_t *d);
void lam_shadow_margins(const lam_ui_style_t *s,
		int32_t *pl, int32_t *pt,
		int32_t *pr, int32_t *pb);

/* Internal CSS engine hooks (ui-css.c) */
void lam_css_apply_node(lam_ui_div_t *div, uint32_t ss, lam_ui_style_t *dst);
void lam_css_apply_inline_str(lam_ui_style_t *dst, uint32_t ss, const char *decls);
int  lam_ui_has_class(const lam_ui_div_t *div, const char *cls, size_t len);

#endif /* LAM_UI_INTERNAL_H */
