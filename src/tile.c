#include "../include/ttypt/lam.h"
#include "../include/ttypt/lam-tm.h"

#include <stdio.h>
#include <string.h>

#include <ttypt/corm.h>
#include <ttypt/stele.h>

static uint32_t tm_hd;

uint32_t
lam_tm_new(uint32_t img_ref, uint32_t w, uint32_t h)
{
	lam_tm_t tm;
	uint32_t img_w, img_h;

	tm.img = img_ref;
	lam_tex_size(&img_w, &img_h, img_ref);
	tm.w = w;
	tm.h = h;
	tm.nx = img_w / w;
	tm.ny = img_h / h;

	uint32_t ref = corm_put(tm_hd, NULL, &tm);
	WARN("tm_load %u: %u %u %u\n", ref, img_ref,
			w, h);
	return ref;
}

void
lam_tile_draw(uint32_t ref, uint32_t idx,
		uint32_t x, uint32_t y,
		uint32_t w, uint32_t h,
		uint32_t rx, uint32_t ry)
{
	const lam_tm_t *tm = corm_get(tm_hd, &ref);

	unsigned tm_x = idx % tm->nx;
	unsigned tm_y = idx / tm->nx;

	if (!w)
		w = tm->w;
	if (!h)
		h = tm->h;

	for (uint32_t iy = 0; iy < ry * h; iy += h)
		for (uint32_t ix = 0; ix < rx * w; ix += w)
			lam_tex_draw_x(tm->img,
					x + ix,
					y + iy,
					tm_x * tm->w,
					tm_y * tm->h,
					tm->w, tm->h,
					w, h, lam_default_tint);
}

void
lam_tile_map_draw(uint32_t ref,
                  const uint16_t *tiles,
                  uint32_t map_w,
                  uint32_t map_h,
                  int32_t start_x,
                  int32_t start_y,
                  uint32_t scale)
{
	const lam_tm_t *tm = corm_get(tm_hd, &ref);
	if (!tm || !tiles || !map_w || !map_h)
		return;

	if (scale == 0)
		scale = 1;

	uint32_t tile_w = tm->w * scale;
	uint32_t tile_h = tm->h * scale;

	for (uint32_t row = 0; row < map_h; row++) {
		int32_t y = start_y + (int32_t)(row * tile_h);
		for (uint32_t col = 0; col < map_w; col++) {
			int32_t x = start_x + (int32_t)(col * tile_w);
			uint16_t tile_idx = tiles[row * map_w + col];
			unsigned tm_x = tile_idx % tm->nx;
			unsigned tm_y = tile_idx / tm->nx;

			lam_tex_draw_x(tm->img,
			               x, y,
			               tm_x * tm->w, tm_y * tm->h,
			               tm->w, tm->h,
			               tile_w, tile_h,
			               lam_default_tint);
		}
	}
}

const lam_tm_t *
lam_tm_get(uint32_t ref)
{
	return corm_get(tm_hd, &ref);
}

void
tile_construct(void)
{
	uint32_t cm_tm = corm_reg(sizeof(lam_tm_t));

	tm_hd = corm_open(NULL, NULL, CM_HNDL, cm_tm, 0xF, CM_AINDEX);
}

void __attribute__((constructor))
constructor(void)
{
	tile_construct();
}
