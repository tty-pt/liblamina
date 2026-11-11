#ifndef LAM_INPUT_H
#define LAM_INPUT_H

typedef void input_init_t(int flags);
typedef void input_deinit_t(void);

typedef struct {
	input_init_t *init;
	input_deinit_t *deinit, *poll;
} lam_input_t;

void input_call(unsigned short code,
		unsigned short value,
		int type);

#endif /* LAM_INPUT_H */
