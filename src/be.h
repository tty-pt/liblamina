#ifndef LAM_BE_H
#define LAM_BE_H

#include <stdint.h>

typedef struct {
	uint32_t size;
	uint8_t *canvas;
	uint8_t channels;
	uint32_t min_x, min_y, max_x, max_y;
} screen_t;

extern screen_t screen;

typedef void lam_be_init_t(uint32_t *w, uint32_t *h);
typedef void lam_be_deinit_t(void);

typedef int lam_be_running_t(void);
typedef void lam_be_set_title_t(const char *title);
typedef void lam_be_resize_t(uint32_t w, uint32_t h);
typedef void lam_be_set_cursor_t(int cursor);
typedef float lam_be_content_scale_t(void);

typedef struct {
	lam_be_init_t *init;
	lam_be_deinit_t *flush, *deinit;
	lam_be_running_t *running;
	lam_be_set_title_t *set_title;
	lam_be_resize_t *resize;
	lam_be_set_cursor_t *set_cursor;
	lam_be_content_scale_t *content_scale;
} lam_be_t;

extern uint32_t lam_width, lam_height;
void lam_fbo_resize(uint32_t w, uint32_t h);
void lam_resize(uint32_t w, uint32_t h);

#endif /* LAM_BE_H */
