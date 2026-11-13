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

typedef struct {
	lam_be_init_t *init;
	lam_be_deinit_t *flush, *deinit;
} lam_be_t;

extern uint32_t lam_width, lam_height;

#endif /* LAM_BE_H */
