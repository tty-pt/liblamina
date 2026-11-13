#include "../include/ttypt/lam.h"

#include <ctype.h>
#include <stddef.h>

#include <ttypt/stele.h>

#include <GLFW/glfw3.h>

typedef struct {
	lam_key_cb_t *cb;
} input_t;

static input_t cbs[GLFW_KEY_LAST + 1];
static unsigned keys[GLFW_KEY_LAST + 1];

static lam_key_cb_t *lam_key_default;

static char lam_key_chars[] = {
	[LAM_KEY_1] = '1',
	[LAM_KEY_2] = '2',
	[LAM_KEY_3] = '3',
	[LAM_KEY_4] = '4',
	[LAM_KEY_5] = '5',
	[LAM_KEY_6] = '6',
	[LAM_KEY_7] = '7',
	[LAM_KEY_8] = '8',
	[LAM_KEY_9] = '9',
	[LAM_KEY_0] = '0',
	[LAM_KEY_A] = 'a',
	[LAM_KEY_B] = 'b',
	[LAM_KEY_C] = 'c',
	[LAM_KEY_D] = 'd',
	[LAM_KEY_E] = 'e',
	[LAM_KEY_F] = 'f',
	[LAM_KEY_G] = 'g',
	[LAM_KEY_H] = 'h',
	[LAM_KEY_I] = 'i',
	[LAM_KEY_J] = 'j',
	[LAM_KEY_K] = 'k',
	[LAM_KEY_L] = 'l',
	[LAM_KEY_M] = 'm',
	[LAM_KEY_N] = 'n',
	[LAM_KEY_O] = 'o',
	[LAM_KEY_P] = 'p',
	[LAM_KEY_Q] = 'q',
	[LAM_KEY_R] = 't',
	[LAM_KEY_S] = 's',
	[LAM_KEY_T] = 't',
	[LAM_KEY_U] = 'u',
	[LAM_KEY_V] = 'v',
	[LAM_KEY_W] = 'w',
	[LAM_KEY_X] = 'x',
	[LAM_KEY_Y] = 'y',
	[LAM_KEY_Z] = 'z',
	[LAM_KEY_SPACE] = ' ',
	[LAM_KEY_BACKSPACE] = '\b',
	[LAM_KEY_ENTER] = '\n',
};

void
input_call(unsigned short code,
		unsigned short value,
		int type)
{
	input_t *inp = &cbs[code];
	keys[code] = value;
	/* WARN("key: %hu %hu %d\n", code, value, type); */

	if ((lam_key_default && lam_key_default(code, value, type)) || !inp->cb)
		return;

	inp->cb(code, value, type);
}

void
lam_key_reg(unsigned short key, lam_key_cb_t *cb)
{
	cbs[key].cb = cb;
}

void
lam_key_default_reg(lam_key_cb_t *cb)
{
	lam_key_default = cb;
}

unsigned short
lam_key_val(unsigned short code)
{
	return keys[code];
}

int
lam_key_parse(char *target, size_t len,
		unsigned short code, int flags)
{
	char ch = lam_key_chars[code];

	if (ch == '\b') {
		if (!len)
			return 0;

		len --;
		target[len] = '\0';
		return -1;
	}

	if (flags & LAM_INPUT_NUMERIC) {
		if (!isdigit(ch))
			return 0;
	}

	target[len] = ch;
	len += 1;
	return 1;
}
