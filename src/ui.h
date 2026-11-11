#ifndef LAM_UI_INTERNAL_H
#define LAM_UI_INTERNAL_H

#include "../include/ttypt/lam-ui.h"
#include <stdint.h>

struct lam_ui_div {
	struct lam_ui_div *parent;
	struct lam_ui_div *first_child;
	struct lam_ui_div *next_sibling;
	struct lam_ui_div *last_child;

	int32_t x, y, w, h;
	uint32_t content_w, content_h;

	const char *class_name;
	const char *text;

	lam_ui_style_t *style;
	const char *overflow;

	int style_calloc;
	void *_cache;

	int needs_layout;

	uint32_t measured_w, measured_h;
	uint32_t measured_cw, measured_ch;
};

void render_div_raw(lam_ui_div_t *d);
void lam_shadow_margins(const lam_ui_style_t *s,
		int32_t *pl, int32_t *pt,
		int32_t *pr, int32_t *pb);

#endif /* LAM_UI_INTERNAL_H */
