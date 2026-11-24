/*
 * font.c — bitmap + FreeType2 proportional UTF-8 font engine
 */

#include "../include/ttypt/lam.h"
#include "../include/ttypt/lam-font.h"
#include "../include/ttypt/lam-tm.h"
#include "../include/ttypt/lam-ui.h"
#include "./gl.h"
#include "./tex.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_LCD_FILTER_H

#include <ttypt/corm.h>

#define LAM_FONT_MAX_FALLBACKS 4
#define LAM_ATLAS_SIZE         1024
#define LAM_ATLAS_MAX_PAGES    8
#define LAM_ATLAS_PAD          1

typedef enum {
	LAM_FONT_BITMAP = 0,
	LAM_FONT_TTF    = 1,
} lam_font_kind_t;

struct lam_glyph {
	uint16_t idx;
};

typedef struct {
	uint64_t key;       /* ((uint64_t)cp << 17) | (lcd << 16) | (phys_px & 0xFFFF); 0 = empty */
	uint32_t glyph_idx; /* FreeType glyph index (for kerning on primary face) */
	uint16_t u0, v0;    /* top-left pixel in atlas page */
	uint16_t w, h;      /* glyph bitmap physical pixel size in atlas */
	int16_t  bearing_x; /* left offset from pen x (physical px) */
	int16_t  bearing_y; /* top offset above baseline (physical px) */
	int16_t  advance;   /* horizontal advance in physical px */
	uint8_t  page;      /* atlas page index */
	uint8_t  is_color;  /* 1 if BGRA color bitmap */
	uint8_t  from_main; /* 1 if glyph came from primary face */
	uint8_t  _pad;
} lam_ttf_glyph_t;

typedef struct {
	GLuint   tex_id;
	uint16_t cur_x;
	uint16_t cur_y;
	uint16_t shelf_h;
} lam_atlas_page_t;

typedef struct {
	FT_Face          face;
	uint32_t         base_px;
	uint32_t         fallbacks[LAM_FONT_MAX_FALLBACKS];
	uint32_t         n_fallbacks;
	lam_atlas_page_t pages[LAM_ATLAS_MAX_PAGES];
	uint32_t         n_pages;
	lam_ttf_glyph_t *table;
	uint32_t         cap;
	uint32_t         count;
	uint32_t         metrics_px;
	uint32_t         ascent;
	uint32_t         line_h;
} lam_ttf_t;

struct lam_font_i {
	lam_font_kind_t kind;
	uint32_t tm_ref;
	uint8_t  first, last;
	uint16_t cell_w;
	uint16_t cell_h;
	uint16_t lineh;
	struct lam_glyph g[256];
	lam_ttf_t *ttf;
};

static uint32_t g_hd_fonts;
static uint32_t g_type_font;
static FT_Library g_ft_lib;

static int ensure_maps(void)
{
	if (g_hd_fonts)
		return 0;

	g_type_font = corm_reg(sizeof(struct lam_font_i));
	g_hd_fonts = corm_open(NULL, NULL,
			CM_HNDL, g_type_font,
			(1u << 8) - 1, CM_AINDEX);
	return g_hd_fonts ? 0 : -1;
}

static inline struct lam_font_i *get_font(uint32_t ref)
{
	if (!g_hd_fonts || ref == CM_MISS)
		return NULL;
	const void *v = corm_get(g_hd_fonts, &ref);
	return (struct lam_font_i *)v;
}

/* ── UTF-8 decoder ──────────────────────────────────────────────────────── */

static uint32_t utf8_next(const unsigned char **pp)
{
	const unsigned char *p = *pp;
	uint32_t c = *p;

	if (c < 0x80) {
		*pp = p + 1;
		return c;
	}
	if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
		uint32_t cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
		*pp = p + 2;
		return cp >= 0x80 ? cp : 0xFFFD;
	}
	if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
		uint32_t cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
		*pp = p + 3;
		return cp >= 0x800 ? cp : 0xFFFD;
	}
	if ((c & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 &&
	    (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
		uint32_t cp = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
		              ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
		*pp = p + 4;
		return (cp >= 0x10000 && cp <= 0x10FFFF) ? cp : 0xFFFD;
	}
	*pp = p + 1;
	return 0xFFFD;
}

/* ── TTF glyph hash table & shelf atlas ────────────────────────────────── */

static inline uint32_t hash_u64(uint64_t x)
{
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdULL;
	x ^= x >> 33;
	x *= 0xc4ceb9fe1a85ec53ULL;
	x ^= x >> 33;
	return (uint32_t)x;
}

static lam_ttf_glyph_t *ttf_table_find(lam_ttf_t *t, uint64_t key)
{
	uint32_t mask = t->cap - 1;
	uint32_t idx = hash_u64(key) & mask;
	for (;;) {
		lam_ttf_glyph_t *e = &t->table[idx];
		if (e->key == 0)
			return NULL;
		if (e->key == key)
			return e;
		idx = (idx + 1) & mask;
	}
}

static lam_ttf_glyph_t *ttf_table_insert(lam_ttf_t *t, const lam_ttf_glyph_t *val)
{
	if (t->count * 2 >= t->cap) {
		uint32_t new_cap = t->cap * 2;
		lam_ttf_glyph_t *nt = calloc(new_cap, sizeof(*nt));
		if (!nt)
			return NULL;
		uint32_t nmask = new_cap - 1;
		for (uint32_t i = 0; i < t->cap; i++) {
			if (t->table[i].key == 0)
				continue;
			uint32_t j = hash_u64(t->table[i].key) & nmask;
			while (nt[j].key != 0)
				j = (j + 1) & nmask;
			nt[j] = t->table[i];
		}
		free(t->table);
		t->table = nt;
		t->cap = new_cap;
	}
	uint32_t mask = t->cap - 1;
	uint32_t idx = hash_u64(val->key) & mask;
	while (t->table[idx].key != 0)
		idx = (idx + 1) & mask;
	t->table[idx] = *val;
	t->count++;
	return &t->table[idx];
}

static float ttf_select_face_size(FT_Face face, uint32_t px_size)
{
	if (FT_IS_SCALABLE(face)) {
		FT_Set_Pixel_Sizes(face, 0, px_size);
		return 1.0f;
	}
	if (face->num_fixed_sizes > 0) {
		int best = 0;
		int best_diff = 1000000;
		for (int i = 0; i < face->num_fixed_sizes; i++) {
			int h = face->available_sizes[i].height;
			int diff = h - (int)px_size;
			if (diff < 0) diff = -diff;
			if (diff < best_diff) {
				best_diff = diff;
				best = i;
			}
		}
		FT_Select_Size(face, best);
		int sh = face->available_sizes[best].height;
		if (sh > 0)
			return (float)px_size / (float)sh;
	}
	return 1.0f;
}

static void ttf_update_metrics(lam_ttf_t *t, uint32_t px_size)
{
	if (px_size == 0)
		px_size = t->base_px;
	if (t->metrics_px == px_size)
		return;

	float scale = ttf_select_face_size(t->face, px_size);
	int asc  = (int)((float)(t->face->size->metrics.ascender  >> 6) * scale + 0.5f);
	int desc = (int)((float)(-t->face->size->metrics.descender >> 6) * scale + 0.5f);
	int lh   = (int)((float)(t->face->size->metrics.height    >> 6) * scale + 0.5f);

	if (asc <= 0)
		asc = (int)(px_size * 4 / 5);
	if (desc < 0)
		desc = 0;
	if (lh < asc + desc)
		lh = asc + desc;
	if (lh < (int)px_size)
		lh = (int)px_size;

	t->metrics_px = px_size;
	t->ascent = (uint32_t)asc;
	t->line_h = (uint32_t)lh;
}

static int ttf_alloc_atlas_page(lam_ttf_t *t)
{
	if (t->n_pages >= LAM_ATLAS_MAX_PAGES)
		return -1;
	lam_atlas_page_t *pg = &t->pages[t->n_pages];
	memset(pg, 0, sizeof(*pg));
	pg->cur_x = LAM_ATLAS_PAD;
	pg->cur_y = LAM_ATLAS_PAD;

	if (lam_has_gl()) {
		glGenTextures(1, &pg->tex_id);
		glBindTexture(GL_TEXTURE_2D, pg->tex_id);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		uint8_t *zeros = calloc(1, (size_t)LAM_ATLAS_SIZE * LAM_ATLAS_SIZE * 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
		             LAM_ATLAS_SIZE, LAM_ATLAS_SIZE, 0,
		             GL_RGBA, GL_UNSIGNED_BYTE, zeros);
		free(zeros);
	}
	return (int)(t->n_pages++);
}

static int ttf_pack_atlas(lam_ttf_t *t, uint32_t gw, uint32_t gh,
                          const uint8_t *rgba,
                          uint8_t *out_page, uint16_t *out_x, uint16_t *out_y)
{
	if (gw + 2 * LAM_ATLAS_PAD > LAM_ATLAS_SIZE ||
	    gh + 2 * LAM_ATLAS_PAD > LAM_ATLAS_SIZE)
		return -1;

	if (t->n_pages == 0) {
		if (ttf_alloc_atlas_page(t) < 0)
			return -1;
	}

	int p_idx = (int)t->n_pages - 1;
	lam_atlas_page_t *pg = &t->pages[p_idx];

	if (pg->cur_x + gw + LAM_ATLAS_PAD > LAM_ATLAS_SIZE) {
		pg->cur_x = LAM_ATLAS_PAD;
		pg->cur_y = (uint16_t)(pg->cur_y + pg->shelf_h + LAM_ATLAS_PAD);
		pg->shelf_h = 0;
	}
	if (pg->cur_y + gh + LAM_ATLAS_PAD > LAM_ATLAS_SIZE) {
		p_idx = ttf_alloc_atlas_page(t);
		if (p_idx < 0)
			return -1;
		pg = &t->pages[p_idx];
	}

	uint16_t ax = pg->cur_x;
	uint16_t ay = pg->cur_y;
	pg->cur_x = (uint16_t)(pg->cur_x + gw + LAM_ATLAS_PAD);
	if (gh > pg->shelf_h)
		pg->shelf_h = (uint16_t)gh;

	if (lam_has_gl() && pg->tex_id && rgba) {
		glBindTexture(GL_TEXTURE_2D, pg->tex_id);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexSubImage2D(GL_TEXTURE_2D, 0, ax, ay, (GLsizei)gw, (GLsizei)gh,
		                GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	}

	*out_page = (uint8_t)p_idx;
	*out_x = ax;
	*out_y = ay;
	return 0;
}

static float font_dpi_scale(void)
{
	float s = lam_get_dpi_scale();
	return (s >= 1.0f && s <= 4.0f) ? s : 1.0f;
}

static uint32_t css_to_phys_px(uint32_t css_px, float scale)
{
	uint32_t p = (uint32_t)lroundf((float)css_px * scale);
	if (p < 1) p = 1;
	if (p > 256) p = 256;
	return p;
}

static lam_ttf_glyph_t *ttf_get_glyph(lam_ttf_t *t, uint32_t cp,
                                      uint32_t phys_px, int want_lcd)
{
	if (phys_px == 0)
		phys_px = t->base_px;
	if (phys_px > 256)
		phys_px = 256;

	uint32_t safe_cp = cp ? cp : 0xFFFD;
	uint64_t key = ((uint64_t)safe_cp << 17) |
	               ((uint64_t)(want_lcd ? 1u : 0u) << 16) |
	               (phys_px & 0xFFFFu);
	lam_ttf_glyph_t *hit = ttf_table_find(t, key);
	if (hit)
		return hit;

	/* Select primary or fallback face */
	FT_Face use_face = t->face;
	FT_UInt gidx = FT_Get_Char_Index(t->face, safe_cp);
	if (gidx == 0) {
		for (uint32_t i = 0; i < t->n_fallbacks; i++) {
			struct lam_font_i *fb = get_font(t->fallbacks[i]);
			if (!fb || fb->kind != LAM_FONT_TTF || !fb->ttf)
				continue;
			FT_UInt fi = FT_Get_Char_Index(fb->ttf->face, safe_cp);
			if (fi != 0) {
				use_face = fb->ttf->face;
				gidx = fi;
				break;
			}
		}
	}

	float scale = ttf_select_face_size(use_face, phys_px);
	if (use_face == t->face)
		t->metrics_px = 0; /* force re-sync if size changed */

	FT_Int32 load_flags = FT_LOAD_RENDER;
	if (FT_HAS_COLOR(use_face))
		load_flags |= FT_LOAD_COLOR;
	else if (want_lcd)
		load_flags |= FT_LOAD_TARGET_LCD;
	else
		load_flags |= FT_LOAD_TARGET_LIGHT;

	lam_ttf_glyph_t g;
	memset(&g, 0, sizeof(g));
	g.key       = key;
	g.glyph_idx = (uint32_t)gidx;
	g.from_main = (use_face == t->face) ? 1 : 0;

	if (FT_Load_Glyph(use_face, gidx, load_flags) != 0) {
		g.advance = (int16_t)(phys_px / 2);
		return ttf_table_insert(t, &g);
	}

	FT_GlyphSlot slot = use_face->glyph;
	FT_Bitmap *bm = &slot->bitmap;

	g.advance   = (int16_t)((float)((slot->advance.x + 32) >> 6) * scale + 0.5f);
	g.bearing_x = (int16_t)((float)slot->bitmap_left * scale);
	g.bearing_y = (int16_t)((float)slot->bitmap_top  * scale);

	uint32_t bw = (bm->pixel_mode == FT_PIXEL_MODE_LCD) ? (bm->width / 3u) : bm->width;
	uint32_t bh = bm->rows;

	if (bw > 0 && bh > 0 && bm->buffer) {
		uint32_t dw = (scale < 0.99f) ? (uint32_t)((float)bw * scale + 0.5f) : bw;
		uint32_t dh = (scale < 0.99f) ? (uint32_t)((float)bh * scale + 0.5f) : bh;
		if (dw == 0) dw = 1;
		if (dh == 0) dh = 1;

		uint8_t *rgba = malloc((size_t)dw * dh * 4);
		if (rgba) {
			int pitch = bm->pitch;
			if (bm->pixel_mode == FT_PIXEL_MODE_BGRA && (dw != bw || dh != bh)) {
				/* Premultiplied-alpha area box filter for crisp color emoji downscaling */
				g.is_color = 1;
				for (uint32_t y = 0; y < dh; y++) {
					uint32_t sy0 = (y * bh) / dh;
					uint32_t sy1 = ((y + 1) * bh + dh - 1) / dh;
					if (sy1 <= sy0) sy1 = sy0 + 1;
					if (sy1 > bh) sy1 = bh;
					for (uint32_t x = 0; x < dw; x++) {
						uint32_t sx0 = (x * bw) / dw;
						uint32_t sx1 = ((x + 1) * bw + dw - 1) / dw;
						if (sx1 <= sx0) sx1 = sx0 + 1;
						if (sx1 > bw) sx1 = bw;
						uint32_t sr = 0, sg = 0, sb = 0, sa = 0, cnt = 0;
						for (uint32_t sy = sy0; sy < sy1; sy++) {
							const uint8_t *row = (pitch >= 0)
								? bm->buffer + (size_t)sy * (size_t)pitch
								: bm->buffer + (size_t)(bh - 1 - sy) * (size_t)(-pitch);
							for (uint32_t sx = sx0; sx < sx1; sx++) {
								sb += row[sx * 4 + 0];
								sg += row[sx * 4 + 1];
								sr += row[sx * 4 + 2];
								sa += row[sx * 4 + 3];
								cnt++;
							}
						}
						uint8_t *dst = rgba + ((size_t)y * dw + x) * 4;
						uint32_t a = cnt ? (sa + cnt / 2) / cnt : 0;
						if (a > 0) {
							uint32_t r = (sr * 255u + sa / 2) / (sa ? sa : 1);
							uint32_t gr = (sg * 255u + sa / 2) / (sa ? sa : 1);
							uint32_t b = (sb * 255u + sa / 2) / (sa ? sa : 1);
							dst[0] = r  > 255 ? 255 : (uint8_t)r;
							dst[1] = gr > 255 ? 255 : (uint8_t)gr;
							dst[2] = b  > 255 ? 255 : (uint8_t)b;
							dst[3] = a  > 255 ? 255 : (uint8_t)a;
						} else {
							dst[0] = dst[1] = dst[2] = dst[3] = 0;
						}
					}
				}
			} else {
				for (uint32_t y = 0; y < dh; y++) {
					uint32_t sy = (dw == bw && dh == bh) ? y : (y * bh) / dh;
					const uint8_t *row = (pitch >= 0)
						? bm->buffer + (size_t)sy * (size_t)pitch
						: bm->buffer + (size_t)(bh - 1 - sy) * (size_t)(-pitch);
					for (uint32_t x = 0; x < dw; x++) {
						uint32_t sx = (dw == bw && dh == bh) ? x : (x * bw) / dw;
						uint8_t *dst = rgba + ((size_t)y * dw + x) * 4;
						if (bm->pixel_mode == FT_PIXEL_MODE_BGRA) {
							uint8_t b  = row[sx * 4 + 0];
							uint8_t gr = row[sx * 4 + 1];
							uint8_t r  = row[sx * 4 + 2];
							uint8_t a  = row[sx * 4 + 3];
							if (a > 0 && a < 255) {
								uint32_t ur = ((uint32_t)r * 255u) / a;
								uint32_t ug = ((uint32_t)gr * 255u) / a;
								uint32_t ub = ((uint32_t)b * 255u) / a;
								r  = ur > 255 ? 255 : (uint8_t)ur;
								gr = ug > 255 ? 255 : (uint8_t)ug;
								b  = ub > 255 ? 255 : (uint8_t)ub;
							}
							dst[0] = r;
							dst[1] = gr;
							dst[2] = b;
							dst[3] = a;
							g.is_color = 1;
						} else if (bm->pixel_mode == FT_PIXEL_MODE_LCD) {
							uint8_t r  = row[sx * 3 + 0];
							uint8_t gr = row[sx * 3 + 1];
							uint8_t b  = row[sx * 3 + 2];
							uint8_t a  = r > gr ? r : gr;
							if (b > a) a = b;
							dst[0] = r;
							dst[1] = gr;
							dst[2] = b;
							dst[3] = a;
						} else if (bm->pixel_mode == FT_PIXEL_MODE_MONO) {
							uint8_t bit = (row[sx >> 3] & (0x80u >> (sx & 7))) ? 255 : 0;
							dst[0] = bit;
							dst[1] = bit;
							dst[2] = bit;
							dst[3] = bit;
						} else {
							uint8_t a = row[sx];
							dst[0] = a;
							dst[1] = a;
							dst[2] = a;
							dst[3] = a;
						}
					}
				}
			}

			if (ttf_pack_atlas(t, dw, dh, rgba, &g.page, &g.u0, &g.v0) == 0) {
				g.w = (uint16_t)dw;
				g.h = (uint16_t)dh;
			}
			free(rgba);
		}
	}

	return ttf_table_insert(t, &g);
}

/* ── Font lifecycle API ─────────────────────────────────────────────────── */

uint32_t lam_font_open(const char *png_path,
		       unsigned cell_w,
		       unsigned cell_h,
		       uint8_t first,
		       uint8_t last)
{
	if (!png_path || cell_w == 0 || cell_h == 0)
		return CM_MISS;
	if (ensure_maps() != 0)
		return CM_MISS;

	struct lam_font_i font;
	memset(&font, 0, sizeof(font));

	font.kind   = LAM_FONT_BITMAP;
	font.first  = first;
	font.last   = last;
	font.cell_w = (uint16_t)cell_w;
	font.cell_h = (uint16_t)cell_h;
	font.lineh  = (uint16_t)cell_h;

	{
		uint32_t img_ref = lam_tex_load(png_path);
		if (img_ref == CM_MISS)
			return CM_MISS;

		uint32_t tm_ref = lam_tm_new(img_ref, cell_w, cell_h);
		const lam_tm_t *tm = lam_tm_get(tm_ref);
		if (!tm)
			return CM_MISS;

		unsigned gx = 0, gy = 0;
		for (int c = font.first; c <= font.last; c++) {
			uint16_t idx = (uint16_t)(gy * tm->nx + gx);
			font.g[c].idx = idx;

			if (++gx == tm->nx) {
				gx = 0;
				gy++;
			}
		}
		font.tm_ref = tm_ref;
	}

	return corm_put(g_hd_fonts, NULL, &font);
}

uint32_t lam_font_open_ttf(const char *ttf_path, uint32_t px_size)
{
	if (!ttf_path || !*ttf_path)
		return CM_MISS;
	if (ensure_maps() != 0)
		return CM_MISS;

	if (!g_ft_lib) {
		if (FT_Init_FreeType(&g_ft_lib) != 0)
			return CM_MISS;
		FT_Library_SetLcdFilter(g_ft_lib, FT_LCD_FILTER_DEFAULT);
	}

	if (px_size == 0)
		px_size = 16;

	lam_ttf_t *t = calloc(1, sizeof(*t));
	if (!t)
		return CM_MISS;

	if (FT_New_Face(g_ft_lib, ttf_path, 0, &t->face) != 0) {
		free(t);
		return CM_MISS;
	}

	t->base_px = px_size;
	t->cap = 512;
	t->table = calloc(t->cap, sizeof(*t->table));
	if (!t->table) {
		FT_Done_Face(t->face);
		free(t);
		return CM_MISS;
	}

	ttf_update_metrics(t, px_size);

	struct lam_font_i font;
	memset(&font, 0, sizeof(font));
	font.kind  = LAM_FONT_TTF;
	font.lineh = (uint16_t)t->line_h;
	font.ttf   = t;

	return corm_put(g_hd_fonts, NULL, &font);
}

int lam_font_add_fallback(uint32_t font_ref, uint32_t fallback_ref)
{
	struct lam_font_i *f = get_font(font_ref);
	struct lam_font_i *fb = get_font(fallback_ref);
	if (!f || !fb || f->kind != LAM_FONT_TTF || fb->kind != LAM_FONT_TTF || !f->ttf)
		return -1;
	if (font_ref == fallback_ref)
		return -1;
	if (f->ttf->n_fallbacks >= LAM_FONT_MAX_FALLBACKS)
		return -1;
	f->ttf->fallbacks[f->ttf->n_fallbacks++] = fallback_ref;
	return 0;
}

int lam_font_is_ttf(uint32_t font_ref)
{
	struct lam_font_i *f = get_font(font_ref);
	return (f && f->kind == LAM_FONT_TTF) ? 1 : 0;
}

uint32_t lam_font_line_height(uint32_t font_ref, uint32_t px_size)
{
	struct lam_font_i *f = get_font(font_ref);
	if (!f)
		return 0;
	if (f->kind == LAM_FONT_TTF && f->ttf) {
		float s = font_dpi_scale();
		uint32_t ppx = css_to_phys_px(px_size ? px_size : f->ttf->base_px, s);
		ttf_update_metrics(f->ttf, ppx);
		return (uint32_t)ceilf((float)f->ttf->line_h / s);
	}
	uint32_t sc = px_size ? px_size : 1;
	return (uint32_t)f->cell_h * sc;
}

uint32_t lam_font_ascent(uint32_t font_ref, uint32_t px_size)
{
	struct lam_font_i *f = get_font(font_ref);
	if (!f)
		return 0;
	if (f->kind == LAM_FONT_TTF && f->ttf) {
		float s = font_dpi_scale();
		uint32_t ppx = css_to_phys_px(px_size ? px_size : f->ttf->base_px, s);
		ttf_update_metrics(f->ttf, ppx);
		return (uint32_t)ceilf((float)f->ttf->ascent / s);
	}
	uint32_t sc = px_size ? px_size : 1;
	return (uint32_t)f->cell_h * sc;
}

void lam_font_close(uint32_t font_ref)
{
	if (!g_hd_fonts)
		return;
	struct lam_font_i *f = get_font(font_ref);
	if (f && f->kind == LAM_FONT_TTF && f->ttf) {
		lam_ttf_t *t = f->ttf;
		if (lam_has_gl()) {
			for (uint32_t i = 0; i < t->n_pages; i++) {
				if (t->pages[i].tex_id)
					glDeleteTextures(1, &t->pages[i].tex_id);
			}
		}
		free(t->table);
		if (t->face)
			FT_Done_Face(t->face);
		free(t);
		f->ttf = NULL;
	}
	corm_del(g_hd_fonts, &font_ref);
}

/* ── Bitmap font iterator (legacy, 100% preserved) ──────────────────────── */

static const char *
lam_font_iterate_bitmap(struct lam_font_i *f, const char *text,
        uint32_t x0, uint32_t y0,
        uint32_t x1, uint32_t y1,
        uint32_t scale,
        lam_ui_white_space_t ws,
        lam_ui_word_break_t wb,
        void (*emit)(void *user, uint32_t x, uint32_t y,
            uint32_t w, uint32_t h, uint16_t glyph_idx),
        void *user,
        uint32_t *max_w_out, uint32_t *max_h_out)
{
	const lam_tm_t *tm = lam_tm_get(f->tm_ref);
	if (!tm)
		return NULL;

	if (scale == 0)
		scale = 1;

	const uint32_t cw = scale * tm->w;
	const uint32_t ch = scale * tm->h;

	if (x1 <= x0 || y1 <= y0 || cw == 0 || ch == 0)
		return text;

	uint32_t cx = x0;
	uint32_t cy = y0;
	uint32_t max_w = 0;
	int in_word = 0;

	const unsigned char *p = (const unsigned char *)text;

	while (*p) {
		unsigned c = *p;

		if (c == '\n') {
			if (ws == LAM_UI_WS_PRE ||
			    ws == LAM_UI_WS_PRE_WRAP ||
			    ws == LAM_UI_WS_PRE_LINE) {
				cx = x0;
				cy += ch;
			}
			p++;
			in_word = 0;
			continue;
		}

		if (cy + ch > y1)
			break;

		if (c == ' ') {
			p++;
			in_word = 0;

			if (cx == x0 &&
			    (ws == LAM_UI_WS_NORMAL ||
			     ws == LAM_UI_WS_PRE_LINE))
				continue;

			cx += cw;
			continue;
		}

		if (ws == LAM_UI_WS_NOWRAP) {
			if (cx + cw > x1)
				break;
		}

		uint32_t word_len = 0;
		uint32_t word_px  = 0;

		if (!in_word) {
			const unsigned char *w = p;
			while (w[word_len] &&
			       w[word_len] != ' ' &&
			       w[word_len] != '\n')
				word_len++;
			word_px = word_len * cw;
		}

		if (ws == LAM_UI_WS_NORMAL ||
		    ws == LAM_UI_WS_PRE_WRAP ||
		    ws == LAM_UI_WS_PRE_LINE) {
			if (!in_word && cx > x0 && cx + word_px > x1) {
				if (wb == LAM_UI_WB_BREAK_ALL) {
				} else if (wb == LAM_UI_WB_BREAK_WORD) {
					if (word_px <= (x1 - x0)) {
						cx = x0;
						cy += ch;
					}
				} else {
					cx = x0;
					cy += ch;
				}
			}

			if (cx + cw > x1 && cx > x0) {
				cx = x0;
				cy += ch;
			}

			if (cy + ch > y1)
				break;
		}

		if (c >= f->first && c <= f->last) {
			uint16_t glyph_idx = f->g[c].idx;
			if (emit)
				emit(user, cx, cy, cw, ch, glyph_idx);
		}

		cx += cw;
		in_word = 1;
		p++;

		if (cx - x0 > max_w)
			max_w = cx - x0;
	}

	if (max_w_out) *max_w_out = max_w;
	if (max_h_out) *max_h_out = (cy - y0) + ch;

	return (p && *p) ? (const char *)p : NULL;
}

static void font_emit_bitmap_draw(
		void *user, uint32_t x, uint32_t y,
		uint32_t w, uint32_t h, uint16_t glyph_idx)
{
	const struct lam_font_i *f = user;
	lam_tile_draw(f->tm_ref, glyph_idx, x, y, w, h, 1, 1);
}

/* ── TTF proportional UTF-8 iterator ────────────────────────────────────── */

static int is_ignorable_cp(uint32_t cp)
{
	return cp == 0xFE0Eu || cp == 0xFE0Fu ||
	       cp == 0x200Bu || cp == 0x200Cu || cp == 0x200Du ||
	       cp == 0xFEFFu;
}

static const char *
lam_font_iterate_ttf(lam_ttf_t *t, const char *text,
        uint32_t x0, uint32_t y0,
        uint32_t x1, uint32_t y1,
        uint32_t px_size,
        lam_ui_white_space_t ws,
        lam_ui_word_break_t wb,
        int do_draw,
        uint32_t *max_w_out, uint32_t *max_h_out)
{
	if (px_size == 0)
		px_size = t->base_px;

	const float scale = font_dpi_scale();
	const float inv_scale = 1.0f / scale;
	const uint32_t phys_px = css_to_phys_px(px_size, scale);
	const int want_lcd = (scale < 1.5f) ? 1 : 0;

	ttf_update_metrics(t, phys_px);
	const uint32_t ch = (uint32_t)ceilf((float)t->line_h * inv_scale);
	const uint32_t ascent_phys = t->ascent;
	const int has_kern = FT_HAS_KERNING(t->face) ? 1 : 0;

	if (x1 <= x0 || y1 <= y0 || ch == 0)
		return text;

	const lam_ttf_glyph_t *sp_g = ttf_get_glyph(t, ' ', phys_px, want_lcd);
	const float space_adv_phys = (sp_g && sp_g->advance > 0)
		? (float)sp_g->advance
		: (float)(phys_px / 3 > 0 ? phys_px / 3 : 1);

	const uint32_t tint = do_draw ? lam_tint_get() : 0xFFFFFFFFu;
	/* Keep color emoji vivid under normal text colors, but preserve black shadow silhouette */
	const uint32_t color_tint = ((tint & 0x00FFFFFFu) == 0u)
		? tint
		: ((tint & 0xFF000000u) | 0x00FFFFFFu);

	const float x0_phys = floorf((float)x0 * scale + 0.5f);
	const float x1_phys = (float)x1 * scale;
	float pen_x_phys = x0_phys;
	uint32_t cy = y0;
	float max_w_phys = 0.0f;
	int in_word = 0;
	FT_UInt prev_gidx = 0;

	const unsigned char *p = (const unsigned char *)text;

	while (*p) {
		const unsigned char *char_start = p;
		uint32_t cp = utf8_next(&p);

		if (cp == '\r' || is_ignorable_cp(cp))
			continue;

		if (cp == '\n') {
			in_word = 0;
			prev_gidx = 0;
			if (ws == LAM_UI_WS_PRE ||
			    ws == LAM_UI_WS_PRE_WRAP ||
			    ws == LAM_UI_WS_PRE_LINE) {
				pen_x_phys = x0_phys;
				cy += ch;
			} else if (pen_x_phys > x0_phys) {
				pen_x_phys += space_adv_phys;
			}
			continue;
		}

		if (cy + ch > y1) {
			p = char_start;
			break;
		}

		if (cp == ' ' || cp == '\t') {
			in_word = 0;
			prev_gidx = 0;
			float adv_phys = (cp == '\t' && (ws == LAM_UI_WS_PRE || ws == LAM_UI_WS_PRE_WRAP))
				? (space_adv_phys * 4.0f) : space_adv_phys;
			if (pen_x_phys <= x0_phys &&
			    (ws == LAM_UI_WS_NORMAL ||
			     ws == LAM_UI_WS_PRE_LINE))
				continue;
			if (ws == LAM_UI_WS_NOWRAP && pen_x_phys + adv_phys > x1_phys + 0.5f) {
				p = char_start;
				break;
			}
			pen_x_phys += adv_phys;
			if (pen_x_phys - x0_phys > max_w_phys)
				max_w_phys = pen_x_phys - x0_phys;
			continue;
		}

		/* Look up current glyph */
		const lam_ttf_glyph_t *g = ttf_get_glyph(t, cp, phys_px, want_lcd);
		float adv_phys = (g && g->advance > 0) ? (float)g->advance : 0.0f;

		float kern_x = 0.0f;
		if (has_kern && prev_gidx && g && g->from_main && g->glyph_idx) {
			ttf_update_metrics(t, phys_px);
			FT_Vector kv = { 0, 0 };
			if (FT_Get_Kerning(t->face, prev_gidx, (FT_UInt)g->glyph_idx,
			                   FT_KERNING_DEFAULT, &kv) == 0)
				kern_x = (float)((kv.x + 32) >> 6);
		}

		if (ws == LAM_UI_WS_NOWRAP) {
			if (pen_x_phys + kern_x + adv_phys > x1_phys + 0.5f) {
				p = char_start;
				break;
			}
		}

		/* Compute proportional word pixel width at word start */
		float word_px_phys = 0.0f;
		if (!in_word && (ws == LAM_UI_WS_NORMAL ||
		                 ws == LAM_UI_WS_PRE_WRAP ||
		                 ws == LAM_UI_WS_PRE_LINE)) {
			const unsigned char *wp = char_start;
			FT_UInt wprev = 0;
			while (*wp) {
				uint32_t wcp = utf8_next(&wp);
				if (wcp == ' ' || wcp == '\t' || wcp == '\n' || wcp == '\r')
					break;
				if (is_ignorable_cp(wcp))
					continue;
				const lam_ttf_glyph_t *wg = ttf_get_glyph(t, wcp, phys_px, want_lcd);
				if (wg) {
					if (has_kern && wprev && wg->from_main && wg->glyph_idx) {
						ttf_update_metrics(t, phys_px);
						FT_Vector kv = { 0, 0 };
						if (FT_Get_Kerning(t->face, wprev, (FT_UInt)wg->glyph_idx,
						                   FT_KERNING_DEFAULT, &kv) == 0)
							word_px_phys += (float)((kv.x + 32) >> 6);
					}
					if (wg->advance > 0)
						word_px_phys += (float)wg->advance;
					wprev = wg->from_main ? (FT_UInt)wg->glyph_idx : 0;
				} else {
					wprev = 0;
				}
			}
			/* Re-fetch g in case hash table resized during word scan */
			g = ttf_get_glyph(t, cp, phys_px, want_lcd);
			adv_phys = (g && g->advance > 0) ? (float)g->advance : 0.0f;
		}

		if (ws == LAM_UI_WS_NORMAL ||
		    ws == LAM_UI_WS_PRE_WRAP ||
		    ws == LAM_UI_WS_PRE_LINE) {
			if (!in_word && pen_x_phys > x0_phys &&
			    pen_x_phys + word_px_phys > x1_phys + 0.5f) {
				if (wb == LAM_UI_WB_BREAK_ALL) {
					/* break glyph by glyph */
				} else if (wb == LAM_UI_WB_BREAK_WORD) {
					if (word_px_phys <= (x1_phys - x0_phys) + 0.5f) {
						pen_x_phys = x0_phys;
						cy += ch;
						prev_gidx = 0;
						kern_x = 0.0f;
					}
				} else {
					pen_x_phys = x0_phys;
					cy += ch;
					prev_gidx = 0;
					kern_x = 0.0f;
				}
			}

			if (pen_x_phys + kern_x + adv_phys > x1_phys + 0.5f &&
			    pen_x_phys > x0_phys) {
				pen_x_phys = x0_phys;
				cy += ch;
				prev_gidx = 0;
				kern_x = 0.0f;
			}

			if (cy + ch > y1) {
				p = char_start;
				break;
			}
		}

		pen_x_phys += kern_x;
		if (pen_x_phys < x0_phys)
			pen_x_phys = x0_phys;

		if (do_draw && g && g->w > 0 && g->h > 0 && g->page < t->n_pages) {
			float gx = (floorf(pen_x_phys + 0.5f) + (float)g->bearing_x) * inv_scale;
			float gy = (floorf((float)cy * scale + 0.5f) + (float)ascent_phys - (float)g->bearing_y) * inv_scale;
			float gw = (float)g->w * inv_scale;
			float gh = (float)g->h * inv_scale;
			if (g->is_color) {
				lam_gl_draw_tex_rect_f(
					t->pages[g->page].tex_id,
					LAM_ATLAS_SIZE, LAM_ATLAS_SIZE,
					g->u0, g->v0, g->w, g->h,
					gx, gy, gw, gh,
					color_tint);
			} else {
				lam_gl_draw_glyph_lcd(
					t->pages[g->page].tex_id,
					LAM_ATLAS_SIZE, LAM_ATLAS_SIZE,
					g->u0, g->v0, g->w, g->h,
					gx, gy, gw, gh,
					tint);
			}
		}

		pen_x_phys += adv_phys;
		in_word = 1;
		prev_gidx = (g && g->from_main) ? (FT_UInt)g->glyph_idx : 0;

		if (pen_x_phys - x0_phys > max_w_phys)
			max_w_phys = pen_x_phys - x0_phys;
	}

	if (max_w_out) *max_w_out = (uint32_t)ceilf(max_w_phys * inv_scale);
	if (max_h_out) *max_h_out = (cy - y0) + ch;

	return (p && *p) ? (const char *)p : NULL;
}

/* ── Public draw / measure entrypoints ──────────────────────────────────── */

const char *lam_font_draw_px(uint32_t font_ref, const char *text,
		uint32_t x0, uint32_t y0,
		uint32_t x1, uint32_t y1,
		uint32_t px_size,
		lam_ui_white_space_t ws,
		lam_ui_word_break_t wb)
{
	if (!text || !*text)
		return NULL;
	struct lam_font_i *f = get_font(font_ref);
	if (!f)
		return NULL;

	uint32_t mw = 0, mh = 0;
	if (f->kind == LAM_FONT_TTF && f->ttf) {
		return lam_font_iterate_ttf(f->ttf, text,
				x0, y0, x1, y1,
				px_size ? px_size : f->ttf->base_px,
				ws, wb, 1, &mw, &mh);
	}
	return lam_font_iterate_bitmap(f, text,
			x0, y0, x1, y1, px_size ? px_size : 1,
			ws, wb, font_emit_bitmap_draw, f, &mw, &mh);
}

const char *lam_font_measure_px(uint32_t *w_out, uint32_t *h_out,
		uint32_t font_ref, const char *text,
		uint32_t x0, uint32_t y0,
		uint32_t x1, uint32_t y1,
		uint32_t px_size,
		lam_ui_white_space_t ws,
		lam_ui_word_break_t wb)
{
	if (w_out) *w_out = 0;
	if (h_out) *h_out = 0;
	if (!text || !*text)
		return NULL;
	struct lam_font_i *f = get_font(font_ref);
	if (!f)
		return NULL;

	if (f->kind == LAM_FONT_TTF && f->ttf) {
		return lam_font_iterate_ttf(f->ttf, text,
				x0, y0, x1, y1,
				px_size ? px_size : f->ttf->base_px,
				ws, wb, 0, w_out, h_out);
	}
	return lam_font_iterate_bitmap(f, text,
			x0, y0, x1, y1, px_size ? px_size : 1,
			ws, wb, NULL, NULL, w_out, h_out);
}

const char *lam_font_draw(uint32_t font_ref, const char *text,
		uint32_t x0, uint32_t y0,
		uint32_t x1, uint32_t y1,
		uint32_t scale,
		lam_ui_white_space_t ws,
		lam_ui_word_break_t wb)
{
	struct lam_font_i *f = get_font(font_ref);
	if (!f || !text || !*text)
		return NULL;
	if (f->kind == LAM_FONT_TTF && f->ttf) {
		uint32_t px = f->ttf->base_px * (scale ? scale : 1);
		return lam_font_draw_px(font_ref, text, x0, y0, x1, y1, px, ws, wb);
	}
	return lam_font_draw_px(font_ref, text, x0, y0, x1, y1, scale ? scale : 1, ws, wb);
}

const char *lam_font_measure(uint32_t *w_out, uint32_t *h_out,
		uint32_t font_ref, const char *text,
		uint32_t x0, uint32_t y0,
		uint32_t x1, uint32_t y1,
		uint32_t scale,
		lam_ui_white_space_t ws,
		lam_ui_word_break_t wb)
{
	if (w_out) *w_out = 0;
	if (h_out) *h_out = 0;
	struct lam_font_i *f = get_font(font_ref);
	if (!f || !text || !*text)
		return NULL;
	if (f->kind == LAM_FONT_TTF && f->ttf) {
		uint32_t px = f->ttf->base_px * (scale ? scale : 1);
		return lam_font_measure_px(w_out, h_out, font_ref, text, x0, y0, x1, y1, px, ws, wb);
	}
	return lam_font_measure_px(w_out, h_out, font_ref, text, x0, y0, x1, y1, scale ? scale : 1, ws, wb);
}
