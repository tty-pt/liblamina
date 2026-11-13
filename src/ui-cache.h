#ifndef LAM_CACHE_H
#define LAM_CACHE_H

#include <stdint.h>
#include <ttypt/lam.h>
#include <ttypt/lam-ui.h>
#include "./gl.h"

/*
 * liblamina render cache
 */

typedef struct lam_cache_entry {
	GLuint tex;
	uint32_t w, h;
	int dirty;
} lam_cache_entry_t;

lam_cache_entry_t *lam_cache_alloc(lam_ui_div_t *root);
void lam_cache_free(lam_cache_entry_t *cache);
void lam_cache_invalidate(lam_ui_div_t *d);
void lam_cache_build(lam_ui_div_t *d);
void lam_cache_draw(const lam_ui_div_t *d, int32_t x, int32_t y);
int lam_cache_valid(const lam_ui_div_t *d);

#endif /* LAM_CACHE_H */
