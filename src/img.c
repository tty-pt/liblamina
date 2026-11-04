#include "../include/ttypt/lam.h"

#include "tex.h"

#include <ttypt/corm.h>
#include <ttypt/stele.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
	char *filename;
	struct img_be *be;
	uint8_t *data;
	uint32_t w, h;
} img_t;

typedef struct {
	const img_t *img;
	uint32_t cx, cy, sw, sh, dw, dh,
		 doffx, doffy;
	uint32_t tint;
} img_ctx_t;

static unsigned img_be_hd, img_hd, img_name_hd;
static uint32_t tint;

void img_be_load(char *ext,
		img_load_t *load,
		img_save_t *save)
{
	img_be_t img_be = {
		.load = load,
		.save = save,
	};

	corm_put(img_be_hd, ext, &img_be);
}

void
img_construct(void) {
	unsigned cm_img_be = corm_reg(sizeof(img_be_t)),
		 cm_img = corm_reg(sizeof(img_t));

	img_be_hd = corm_open(NULL, NULL, CM_STR,
			cm_img_be, 0xF, 0);

	img_hd = corm_open(NULL, NULL, CM_HNDL,
			cm_img, 0xF, CM_AINDEX);

	img_name_hd = corm_open(NULL, NULL, CM_STR,
			CM_HNDL, 0xF, 0);

	tint = lam_default_tint;
}

__attribute__((constructor))
static void
construct(void) {
	img_construct();
}

void img_load_all(void)
{
	const void *key, *val;
	uint32_t it;

	it = corm_iter(img_name_hd, NULL, 0);
	while (corm_next(&key, &val, it))
		lam_tex_load((const char *) key);
}

void
img_deinit(void)
{
	corm_close(img_be_hd);
	corm_close(img_hd);
	corm_close(img_name_hd);
}

unsigned
img_new(uint8_t **data,
		const char *filename,
		uint32_t w, uint32_t h,
		unsigned flags)
{
	const unsigned *ref_r;
	unsigned ref;
	img_t img;

	img.w = w;
	img.h = h;
	img.data = malloc(w * h * 4);
	img.filename = strdup(filename);

	if (data)
		*data = img.data;

	ref_r = corm_get(img_name_hd, filename);
	ref = corm_put(img_hd, ref_r, &img);
	corm_put(img_name_hd, img.filename, &ref);

	lam_tex_reg(ref, (flags & IMG_LOAD) ? NULL : img.data,
			img.w, img.h);

	return ref;
}

unsigned lam_tex_load(const char *filename) {
	const char *ext = strrchr(filename, '.');
	img_be_t *be;
	unsigned ref;
	const unsigned *ref_r;
	img_t *img;

	ref_r = corm_get(img_name_hd, filename);
	if (ref_r && corm_get(img_hd, ref_r))
		return *ref_r;

	CBUG(!ext, "IMG: invalid filename %s\n", filename);

	be = (img_be_t *) corm_get(img_be_hd, ext + 1);
	CBUG(!be, "IMG: %s backend not present.\n", ext);

	ref = be->load(filename);
	img = (img_t *) corm_get(img_hd, &ref);
	img->be = be;

	WARN("img_load %u: %s\n", ref, filename);

	return ref;
}

void
lam_tex_save(unsigned ref)
{
	const img_t *img = corm_get(img_hd, &ref);

	img->be->save(img->filename, img->data,
			img->w, img->h);
}

const img_t *
img_get(unsigned ref)
{
	return corm_get(img_hd, &ref);
}

static inline uint8_t *
_img_pick(const img_t *img, uint32_t x, uint32_t y)
{
	uint8_t *pixel = &img->data[
		(y * img->w + x) * 4
	];

	return pixel;
}

void lam_tex_draw(uint32_t ref, int32_t x, int32_t y,
		uint32_t dw, uint32_t dh)
{
	const img_t *img = corm_get(img_hd, &ref);

	lam_tex_draw_x(ref, x, y, 0, 0,
			img->w, img->h, dw, dh, lam_default_tint);
}

void
lam_tint(uint32_t atint)
{
	tint = atint;
}

void
lam_tex_size(uint32_t *w, uint32_t *h, unsigned ref)
{
	const img_t *img = corm_get(img_hd, &ref);

	*w = img->w;
	*h = img->h;
}

uint32_t
lam_tex_pick(unsigned ref, uint32_t x, uint32_t y)
{
	const img_t *img = corm_get(img_hd, &ref);
	uint8_t *color = _img_pick(img, x, y);

	return color[0]
		| (color[1] << 8)
		| (color[2] << 16)
		| (color[3] << 24);
}

void
lam_tex_paint(unsigned ref, uint32_t x, uint32_t y, uint32_t c)
{
	const img_t *img = corm_get(img_hd, &ref);
	uint8_t *color = _img_pick(img, x, y);

	color[0] = c & 0xFF;
	color[1] = (c >> 8) & 0xFF;
	color[2] = (c >> 16) & 0xFF;
	color[3] = (c >> 24) & 0xFF;

	lam_tex_upd(ref, x, y, 1, 1, color);
}

void
img_del(unsigned ref)
{
	lam_tex_ureg(ref);

	const img_t *img = corm_get(img_hd, &ref);
	corm_del(img_name_hd, img->filename);
	free(img->filename);
	free(img->data);
	corm_del(img_hd, &ref);
}
