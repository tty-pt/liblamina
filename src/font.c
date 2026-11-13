/*
 * lam-font.c — bitmap-only font loader
 * Loads a pre-rendered tilemap (e.g., from BDF-to-PNG) instead of using FreeType.
 */

#include "../include/ttypt/lam.h"
#include "../include/ttypt/lam-font.h"
#include "../include/ttypt/lam-tm.h"
#include "../include/ttypt/lam-ui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/corm.h>

struct lam_glyph {
	uint16_t idx;
};

struct lam_font_i {
	uint32_t tm_ref;
	uint8_t	first, last;
	uint16_t cell_w;	/* unscaled cell width (as given on open) */
	uint16_t cell_h;	/* unscaled cell height (as given on open) */
	uint16_t lineh;		/* alias for cell_h for legacy users */
	struct lam_glyph g[256];
};

static uint32_t g_hd_fonts;
static uint32_t g_type_font;

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
	const void *v = corm_get(g_hd_fonts, &ref);
	return (struct lam_font_i *)v;
}

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

	font.first  = first;
	font.last   = last;
	font.cell_w = (uint16_t)cell_w;
	font.cell_h = (uint16_t)cell_h;
	font.lineh  = (uint16_t)cell_h;

	/* load atlas and create tilemap */
	{
		uint32_t img_ref = lam_tex_load(png_path);
		if (img_ref == CM_MISS)
			return CM_MISS;

		uint32_t tm_ref = lam_tm_new(img_ref, cell_w, cell_h);
		const lam_tm_t *tm = lam_tm_get(tm_ref);
		if (!tm)
			return CM_MISS;

		/* fill glyph -> tile index map (grid scanline order) */
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

	/* store font */
	{
		uint32_t ref = corm_put(g_hd_fonts, NULL, &font);
		return ref;
	}
}

void lam_font_close(uint32_t font_ref)
{
	if (!g_hd_fonts)
		return;
	corm_del(g_hd_fonts, &font_ref);
}

/*
 * lam_font_iterate — layout + rendering core
 * Supports white-space and word-break.
 */
static const char *
lam_font_iterate(uint32_t font_ref, const char *text,
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
	*max_w_out = 0;
	*max_h_out = 0;

	if (!text || !*text)
		return NULL;

	struct lam_font_i *f = get_font(font_ref);
	if (!f)
		return NULL;

	const lam_tm_t *tm = lam_tm_get(f->tm_ref);
	if (!tm)
		return NULL;

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

		/* newline */
		if (c == '\n') {
			if (ws == LAM_UI_WS_PRE ||
					ws == LAM_UI_WS_PRE_WRAP ||
					ws == LAM_UI_WS_PRE_LINE)
			{
				cx = x0;
				cy += ch;
			}
			p++;
			continue;
		}

		if (cy + ch > y1)
			break;

		/* spaces */
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

		/* nowrap */
		if (ws == LAM_UI_WS_NOWRAP) {
			if (cx + cw > x1) {
				break;
			}
		}

		/* compute next word length/pixels if at word start */
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

		/* line wrapping logic */
		if (ws == LAM_UI_WS_NORMAL ||
				ws == LAM_UI_WS_PRE_WRAP ||
				ws == LAM_UI_WS_PRE_LINE)
		{
			/* does the word fit? */
			if (!in_word && cx > x0 && cx + word_px > x1) {
				if (wb == LAM_UI_WB_BREAK_ALL) {
					/* don't wrap line immediately; break glyph by glyph */
				} else if (wb == LAM_UI_WB_BREAK_WORD) {
					/* wrap if word fits on next line alone */
					if (word_px <= (x1 - x0)) {
						cx = x0;
						cy += ch;
					}
				} else { /* LAM_UI_WB_NORMAL / KEEP_ALL */
					cx = x0;
					cy += ch;
				}
			}

			/* does this single glyph fit? */
			if (cx + cw > x1 && cx > x0) {
				cx = x0;
				cy += ch;
			}

			if (cy + ch > y1)
				break;
		}

		/* emit glyph */
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

	*max_w_out = max_w;
	*max_h_out = (cy - y0) + ch;

	return (p && *p) ? (const char *)p : NULL;
}

static void font_emit_draw(
		void *user, uint32_t x, uint32_t y,
		uint32_t w, uint32_t h, uint16_t glyph_idx)
{
	const struct lam_font_i *f = user;
	lam_tile_draw(f->tm_ref, glyph_idx, x, y, w, h, 1, 1);
}

const char *lam_font_draw(uint32_t font_ref, const char *text,
		uint32_t x0, uint32_t y0,
		uint32_t x1, uint32_t y1,
		uint32_t scale,
		lam_ui_white_space_t ws,
		lam_ui_word_break_t wb)
{
	struct lam_font_i *f = get_font(font_ref);
	uint32_t mw, mh;
	if (!f)
		return NULL;

	return lam_font_iterate(font_ref, text,
			x0, y0, x1, y1, scale,
			ws, wb,
			font_emit_draw, f, &mw, &mh);
}

const char *lam_font_measure(uint32_t *w_out, uint32_t *h_out,
		uint32_t font_ref, const char *text,
		uint32_t x0, uint32_t y0,
		uint32_t x1, uint32_t y1,
		uint32_t scale,
		lam_ui_white_space_t ws,
		lam_ui_word_break_t wb)
{
	return lam_font_iterate(font_ref, text,
			x0, y0, x1, y1, scale,
			ws, wb,
			NULL, NULL, w_out, h_out);
}
