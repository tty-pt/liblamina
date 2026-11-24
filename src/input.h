#ifndef LAM_INPUT_H
#define LAM_INPUT_H

typedef void input_init_t(int flags);
typedef void input_deinit_t(void);

typedef struct {
	input_init_t *init;
	input_deinit_t *deinit, *poll;
} lam_input_t;

#include "../include/ttypt/lam.h"

void input_call(unsigned short code,
		unsigned short value,
		int type);

void input_emit_event(const lam_event_t *ev);
void input_set_mouse_pos(int32_t x, int32_t y);

#endif /* LAM_INPUT_H */
