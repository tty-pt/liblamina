/*
 * Pure-C baseline JPEG decoder backend for liblamina ("jpeg" / "jpg").
 * Supports 8-bit baseline DCT (SOF0), 1-component grayscale and 3-component
 * YCbCr with arbitrary sampling factors (4:4:4, 4:2:2, 4:2:0, 4:4:0),
 * restart intervals (DRI/RSTn), and zero external library dependencies.
 */

#include "../include/ttypt/lam.h"
#include "tex.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ttypt/corm.h>
#include <ttypt/stele.h>

typedef struct {
	uint8_t  bits[17];
	uint8_t  huffval[256];
	int      maxcode[18];
	int      valoffset[18];
	uint8_t  fast_sym[256];
	uint8_t  fast_len[256];
	int      valid;
} jpg_huff_t;

typedef struct {
	uint8_t id;
	uint8_t h_samp;
	uint8_t v_samp;
	uint8_t q_idx;
	uint8_t dc_idx;
	uint8_t ac_idx;
	int     dc_pred;
} jpg_comp_t;

typedef struct {
	const uint8_t *buf;
	size_t         len;
	size_t         pos;

	uint32_t       bit_buf;
	int            bit_cnt;

	uint16_t       width;
	uint16_t       height;
	uint8_t        ncomp;
	jpg_comp_t     comp[4];
	int            max_h;
	int            max_v;

	uint16_t       qt[4][64];
	int            qt_valid[4];
	jpg_huff_t     dc_ht[4];
	jpg_huff_t     ac_ht[4];

	uint16_t       restart_interval;
	int            error;
} jpg_dec_t;

static const uint8_t ZIGZAG[64] = {
	 0,  1,  8, 16,  9,  2,  3, 10,
	17, 24, 32, 25, 18, 11,  4,  5,
	12, 19, 26, 33, 40, 48, 41, 34,
	27, 20, 13,  6,  7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36,
	29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46,
	53, 60, 61, 54, 47, 55, 62, 63
};

static inline uint8_t clamp_u8(int v)
{
	if (v < 0) return 0;
	if (v > 255) return 255;
	return (uint8_t)v;
}

static inline uint16_t rd_be16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static int build_huff(jpg_huff_t *h)
{
	memset(h->fast_len, 0, sizeof(h->fast_len));
	int code = 0, k = 0;
	for (int l = 1; l <= 16; l++) {
		if (h->bits[l] == 0) {
			h->maxcode[l] = -1;
			h->valoffset[l] = 0;
		} else {
			h->valoffset[l] = k - code;
			for (int i = 0; i < h->bits[l]; i++) {
				if (k >= 256) return 0;
				if (l <= 8) {
					int shift = 8 - l;
					int base = code << shift;
					int cnt = 1 << shift;
					for (int j = 0; j < cnt; j++) {
						h->fast_sym[base + j] = h->huffval[k];
						h->fast_len[base + j] = (uint8_t)l;
					}
				}
				code++;
				k++;
			}
			h->maxcode[l] = code - 1;
		}
		code <<= 1;
	}
	h->valid = 1;
	return 1;
}

static void fill_bits(jpg_dec_t *d)
{
	while (d->bit_cnt <= 24) {
		uint8_t b = 0;
		if (d->pos < d->len) {
			b = d->buf[d->pos++];
			if (b == 0xFF) {
				while (d->pos < d->len && d->buf[d->pos] == 0xFF)
					d->pos++;
				if (d->pos < d->len) {
					uint8_t next = d->buf[d->pos++];
					if (next == 0x00) {
						/* Byte-stuffed 0xFF data byte */
					} else {
						/* Marker encountered: rewind before 0xFF and pad 0 */
						d->pos -= 2;
						b = 0;
					}
				} else {
					b = 0;
				}
			}
		}
		d->bit_buf = (d->bit_buf << 8) | b;
		d->bit_cnt += 8;
	}
}

static inline uint32_t peek_bits(jpg_dec_t *d, int n)
{
	if (d->bit_cnt < n) fill_bits(d);
	return (d->bit_buf >> (d->bit_cnt - n)) & ((1u << n) - 1u);
}

static inline uint32_t get_bits(jpg_dec_t *d, int n)
{
	if (n == 0) return 0;
	if (d->bit_cnt < n) fill_bits(d);
	d->bit_cnt -= n;
	return (d->bit_buf >> d->bit_cnt) & ((1u << n) - 1u);
}

static int decode_huff(jpg_dec_t *d, const jpg_huff_t *h)
{
	if (!h->valid) { d->error = 1; return 0; }
	if (d->bit_cnt < 16) fill_bits(d);

	uint32_t top8 = (d->bit_buf >> (d->bit_cnt - 8)) & 0xFFu;
	uint8_t fl = h->fast_len[top8];
	if (fl) {
		d->bit_cnt -= fl;
		return h->fast_sym[top8];
	}

	int code = 0;
	for (int l = 1; l <= 16; l++) {
		code = (code << 1) | (int)get_bits(d, 1);
		if (code <= h->maxcode[l]) {
			int idx = code + h->valoffset[l];
			if (idx < 0 || idx >= 256) { d->error = 1; return 0; }
			return h->huffval[idx];
		}
	}
	d->error = 1;
	return 0;
}

static inline int extend_sign(int val, int bits)
{
	if (bits == 0) return 0;
	if (val < (1 << (bits - 1)))
		val -= (1 << bits) - 1;
	return val;
}

/* Fixed-point 1D/2D IDCT (standard Chen/Loeffler integer butterfly) */
#define W1 2841 /* 2048 * sqrt(2) * cos(1*pi/16) */
#define W2 2676 /* 2048 * sqrt(2) * cos(2*pi/16) */
#define W3 2408 /* 2048 * sqrt(2) * cos(3*pi/16) */
#define W5 1609 /* 2048 * sqrt(2) * cos(5*pi/16) */
#define W6 1108 /* 2048 * sqrt(2) * cos(6*pi/16) */
#define W7 565  /* 2048 * sqrt(2) * cos(7*pi/16) */

static void idct_row(int *b)
{
	int x0, x1 = b[4] << 11, x2 = b[6], x3 = b[2];
	int x4 = b[1], x5 = b[7], x6 = b[5], x7 = b[3];
	if (!(x1 | x2 | x3 | x4 | x5 | x6 | x7)) {
		int v = b[0] << 3;
		b[0] = b[1] = b[2] = b[3] = b[4] = b[5] = b[6] = b[7] = v;
		return;
	}
	x0 = (b[0] << 11) + 128;
	int x8 = W7 * (x4 + x5);
	x4 = x8 + (W1 - W7) * x4;
	x5 = x8 - (W1 + W7) * x5;
	x8 = W3 * (x6 + x7);
	x6 = x8 - (W3 - W5) * x6;
	x7 = x8 - (W3 + W5) * x7;

	x8 = x0 + x1; x0 -= x1;
	x1 = W6 * (x3 + x2);
	x2 = x1 - (W2 + W6) * x2;
	x3 = x1 + (W2 - W6) * x3;
	x1 = x4 + x6; x4 -= x6;
	x6 = x5 + x7; x5 -= x7;

	x7 = x8 + x3; x8 -= x3;
	x3 = x0 + x2; x0 -= x2;
	x2 = (181 * (x4 + x5) + 128) >> 8;
	x4 = (181 * (x4 - x5) + 128) >> 8;

	b[0] = (x7 + x1) >> 8;
	b[1] = (x3 + x2) >> 8;
	b[2] = (x0 + x4) >> 8;
	b[3] = (x8 + x6) >> 8;
	b[4] = (x8 - x6) >> 8;
	b[5] = (x0 - x4) >> 8;
	b[6] = (x3 - x2) >> 8;
	b[7] = (x7 - x1) >> 8;
}

static void idct_col(const int *b, uint8_t *out, int stride)
{
	int x0, x1 = b[32] << 8, x2 = b[48], x3 = b[16];
	int x4 = b[8], x5 = b[56], x6 = b[40], x7 = b[24];
	if (!(x1 | x2 | x3 | x4 | x5 | x6 | x7)) {
		uint8_t v = clamp_u8(((b[0] + 32) >> 6) + 128);
		for (int i = 0; i < 8; i++)
			out[i * stride] = v;
		return;
	}
	x0 = (b[0] << 8) + 8192;
	int x8 = W7 * (x4 + x5) + 4;
	x4 = (x8 + (W1 - W7) * x4) >> 3;
	x5 = (x8 - (W1 + W7) * x5) >> 3;
	x8 = W3 * (x6 + x7) + 4;
	x6 = (x8 - (W3 - W5) * x6) >> 3;
	x7 = (x8 - (W3 + W5) * x7) >> 3;

	x8 = x0 + x1; x0 -= x1;
	x1 = W6 * (x3 + x2) + 4;
	x2 = (x1 - (W2 + W6) * x2) >> 3;
	x3 = (x1 + (W2 - W6) * x3) >> 3;
	x1 = x4 + x6; x4 -= x6;
	x6 = x5 + x7; x5 -= x7;

	x7 = x8 + x3; x8 -= x3;
	x3 = x0 + x2; x0 -= x2;
	x2 = (181 * (x4 + x5) + 128) >> 8;
	x4 = (181 * (x4 - x5) + 128) >> 8;

	out[0 * stride] = clamp_u8(((x7 + x1) >> 14) + 128);
	out[1 * stride] = clamp_u8(((x3 + x2) >> 14) + 128);
	out[2 * stride] = clamp_u8(((x0 + x4) >> 14) + 128);
	out[3 * stride] = clamp_u8(((x8 + x6) >> 14) + 128);
	out[4 * stride] = clamp_u8(((x8 - x6) >> 14) + 128);
	out[5 * stride] = clamp_u8(((x0 - x4) >> 14) + 128);
	out[6 * stride] = clamp_u8(((x3 - x2) >> 14) + 128);
	out[7 * stride] = clamp_u8(((x7 - x1) >> 14) + 128);
}

static void decode_block(jpg_dec_t *d, jpg_comp_t *c, uint8_t out[64])
{
	int block[64];
	memset(block, 0, sizeof(block));

	const uint16_t *qt = d->qt[c->q_idx];
	int s = decode_huff(d, &d->dc_ht[c->dc_idx]);
	if (d->error) return;
	if (s > 15) { d->error = 1; return; }
	int diff = s ? extend_sign((int)get_bits(d, s), s) : 0;
	c->dc_pred += diff;
	block[0] = c->dc_pred * (int)qt[0];

	int k = 1;
	while (k < 64) {
		int rs = decode_huff(d, &d->ac_ht[c->ac_idx]);
		if (d->error) return;
		int r = rs >> 4;
		int sz = rs & 0x0F;
		if (sz == 0) {
			if (r == 15) {
				k += 16;
			} else {
				break; /* EOB */
			}
		} else {
			k += r;
			if (k >= 64) break;
			int v = extend_sign((int)get_bits(d, sz), sz);
			block[ZIGZAG[k]] = v * (int)qt[k];
			k++;
		}
	}

	for (int row = 0; row < 8; row++)
		idct_row(block + row * 8);
	for (int col = 0; col < 8; col++)
		idct_col(block + col, out + col, 8);
}

static int parse_dqt(jpg_dec_t *d, const uint8_t *p, size_t seg_len)
{
	size_t off = 0;
	while (off < seg_len) {
		uint8_t pq_tq = p[off++];
		int pq = pq_tq >> 4;
		int tq = pq_tq & 0x0F;
		if (tq >= 4) return 0;
		size_t need = pq ? 128 : 64;
		if (off + need > seg_len) return 0;
		for (int i = 0; i < 64; i++) {
			d->qt[tq][i] = pq ? rd_be16(p + off + i * 2) : p[off + i];
		}
		d->qt_valid[tq] = 1;
		off += need;
	}
	return 1;
}

static int parse_dht(jpg_dec_t *d, const uint8_t *p, size_t seg_len)
{
	size_t off = 0;
	while (off < seg_len) {
		if (off + 17 > seg_len) return 0;
		uint8_t tc_th = p[off++];
		int tc = tc_th >> 4;
		int th = tc_th & 0x0F;
		if (tc > 1 || th >= 4) return 0;
		jpg_huff_t *h = tc ? &d->ac_ht[th] : &d->dc_ht[th];
		int total = 0;
		for (int i = 1; i <= 16; i++) {
			h->bits[i] = p[off++];
			total += h->bits[i];
		}
		if (total > 256 || off + (size_t)total > seg_len) return 0;
		memcpy(h->huffval, p + off, (size_t)total);
		off += (size_t)total;
		if (!build_huff(h)) return 0;
	}
	return 1;
}

static int parse_sof0(jpg_dec_t *d, const uint8_t *p, size_t seg_len)
{
	if (seg_len < 6) return 0;
	uint8_t prec = p[0];
	if (prec != 8) return 0;
	d->height = rd_be16(p + 1);
	d->width  = rd_be16(p + 3);
	d->ncomp  = p[5];
	if (d->width == 0 || d->height == 0 ||
	    (d->ncomp != 1 && d->ncomp != 3) ||
	    seg_len < (size_t)(6 + d->ncomp * 3))
		return 0;

	d->max_h = 1;
	d->max_v = 1;
	for (int i = 0; i < d->ncomp; i++) {
		const uint8_t *cp = p + 6 + i * 3;
		d->comp[i].id     = cp[0];
		d->comp[i].h_samp = cp[1] >> 4;
		d->comp[i].v_samp = cp[1] & 0x0F;
		d->comp[i].q_idx  = cp[2];
		if (d->comp[i].h_samp < 1 || d->comp[i].h_samp > 4 ||
		    d->comp[i].v_samp < 1 || d->comp[i].v_samp > 4 ||
		    d->comp[i].q_idx >= 4)
			return 0;
		if (d->comp[i].h_samp > d->max_h) d->max_h = d->comp[i].h_samp;
		if (d->comp[i].v_samp > d->max_v) d->max_v = d->comp[i].v_samp;
	}
	return 1;
}

static int decode_sos(jpg_dec_t *d, const uint8_t *p, size_t seg_len,
                      uint8_t **out_rgba)
{
	if (seg_len < 1 || d->width == 0 || d->height == 0) return 0;
	uint8_t ns = p[0];
	if (ns != d->ncomp || seg_len < (size_t)(1 + ns * 2 + 3)) return 0;

	for (int i = 0; i < ns; i++) {
		uint8_t cid = p[1 + i * 2];
		uint8_t td_ta = p[2 + i * 2];
		int found = 0;
		for (int c = 0; c < d->ncomp; c++) {
			if (d->comp[c].id == cid) {
				d->comp[c].dc_idx = td_ta >> 4;
				d->comp[c].ac_idx = td_ta & 0x0F;
				d->comp[c].dc_pred = 0;
				if (d->comp[c].dc_idx >= 4 || d->comp[c].ac_idx >= 4 ||
				    !d->qt_valid[d->comp[c].q_idx])
					return 0;
				found = 1;
				break;
			}
		}
		if (!found) return 0;
	}

	uint8_t *rgba = malloc((size_t)d->width * (size_t)d->height * 4);
	if (!rgba) return 0;

	int mcu_w = d->max_h * 8;
	int mcu_h = d->max_v * 8;
	int mcus_x = (d->width  + mcu_w - 1) / mcu_w;
	int mcus_y = (d->height + mcu_h - 1) / mcu_h;

	uint8_t comp_buf[3][32 * 32];
	int rst_todo = d->restart_interval;

	d->bit_buf = 0;
	d->bit_cnt = 0;

	for (int my = 0; my < mcus_y && !d->error; my++) {
		for (int mx = 0; mx < mcus_x && !d->error; mx++) {
			for (int c = 0; c < d->ncomp; c++) {
				int hs = d->comp[c].h_samp;
				int vs = d->comp[c].v_samp;
				int cw = hs * 8;
				for (int bv = 0; bv < vs; bv++) {
					for (int bh = 0; bh < hs; bh++) {
						uint8_t blk[64];
						decode_block(d, &d->comp[c], blk);
						if (d->error) break;
						for (int r = 0; r < 8; r++) {
							memcpy(&comp_buf[c][(bv * 8 + r) * cw + bh * 8],
							       blk + r * 8, 8);
						}
					}
				}
			}
			if (d->error) break;

			int base_x = mx * mcu_w;
			int base_y = my * mcu_h;
			int lim_x = base_x + mcu_w <= d->width  ? mcu_w : (d->width  - base_x);
			int lim_y = base_y + mcu_h <= d->height ? mcu_h : (d->height - base_y);

			if (d->ncomp == 1) {
				for (int py = 0; py < lim_y; py++) {
					uint8_t *dst = rgba + ((size_t)(base_y + py) * d->width + (size_t)base_x) * 4;
					const uint8_t *src = comp_buf[0] + py * mcu_w;
					for (int px = 0; px < lim_x; px++) {
						uint8_t y = src[px];
						dst[px * 4 + 0] = y;
						dst[px * 4 + 1] = y;
						dst[px * 4 + 2] = y;
						dst[px * 4 + 3] = 255;
					}
				}
			} else {
				int cw0 = d->comp[0].h_samp * 8, ch0 = d->comp[0].v_samp * 8;
				int cw1 = d->comp[1].h_samp * 8, ch1 = d->comp[1].v_samp * 8;
				int cw2 = d->comp[2].h_samp * 8, ch2 = d->comp[2].v_samp * 8;
				for (int py = 0; py < lim_y; py++) {
					uint8_t *dst = rgba + ((size_t)(base_y + py) * d->width + (size_t)base_x) * 4;
					int y0 = (py * ch0) / mcu_h;
					int y1 = (py * ch1) / mcu_h;
					int y2 = (py * ch2) / mcu_h;
					for (int px = 0; px < lim_x; px++) {
						int x0 = (px * cw0) / mcu_w;
						int x1 = (px * cw1) / mcu_w;
						int x2 = (px * cw2) / mcu_w;
						int Y  = (int)comp_buf[0][y0 * cw0 + x0];
						int Cb = (int)comp_buf[1][y1 * cw1 + x1] - 128;
						int Cr = (int)comp_buf[2][y2 * cw2 + x2] - 128;

						int R = Y + ((359 * Cr) >> 8);
						int G = Y - ((88 * Cb + 183 * Cr) >> 8);
						int B = Y + ((454 * Cb) >> 8);
						dst[px * 4 + 0] = clamp_u8(R);
						dst[px * 4 + 1] = clamp_u8(G);
						dst[px * 4 + 2] = clamp_u8(B);
						dst[px * 4 + 3] = 255;
					}
				}
			}

			if (d->restart_interval > 0) {
				rst_todo--;
				if (rst_todo == 0 && !(mx == mcus_x - 1 && my == mcus_y - 1)) {
					rst_todo = d->restart_interval;
					d->bit_buf = 0;
					d->bit_cnt = 0;
					for (int c = 0; c < d->ncomp; c++)
						d->comp[c].dc_pred = 0;
					/* Advance past 0xFF 0xDn restart marker */
					while (d->pos + 1 < d->len && d->buf[d->pos] != 0xFF)
						d->pos++;
					while (d->pos < d->len && d->buf[d->pos] == 0xFF)
						d->pos++;
					if (d->pos < d->len)
						d->pos++;
				}
			}
		}
	}

	if (d->error) {
		free(rgba);
		return 0;
	}
	*out_rgba = rgba;
	return 1;
}

static unsigned jpegi_load(const char *filename)
{
	FILE *fp = fopen(filename, "rb");
	if (!fp) return CM_MISS;

	if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return CM_MISS; }
	long fsz = ftell(fp);
	if (fsz < 4 || fsz > 64 * 1024 * 1024) { fclose(fp); return CM_MISS; }
	rewind(fp);

	uint8_t *raw = malloc((size_t)fsz);
	if (!raw) { fclose(fp); return CM_MISS; }
	size_t nread = fread(raw, 1, (size_t)fsz, fp);
	fclose(fp);
	if (nread != (size_t)fsz || raw[0] != 0xFF || raw[1] != 0xD8) {
		free(raw);
		return CM_MISS;
	}

	jpg_dec_t dec;
	memset(&dec, 0, sizeof(dec));
	dec.buf = raw;
	dec.len = nread;
	dec.pos = 2;

	uint8_t *rgba = NULL;
	int decoded = 0;

	while (dec.pos + 1 < dec.len && !decoded) {
		if (dec.buf[dec.pos] != 0xFF) { dec.pos++; continue; }
		while (dec.pos < dec.len && dec.buf[dec.pos] == 0xFF) dec.pos++;
		if (dec.pos >= dec.len) break;
		uint8_t marker = dec.buf[dec.pos++];

		if (marker == 0xD9) break; /* EOI */
		if (marker == 0x00 || (marker >= 0xD0 && marker <= 0xD8)) continue;

		if (dec.pos + 2 > dec.len) break;
		uint16_t seg_total = rd_be16(dec.buf + dec.pos);
		if (seg_total < 2 || dec.pos + seg_total > dec.len) break;
		const uint8_t *seg = dec.buf + dec.pos + 2;
		size_t seg_len = (size_t)seg_total - 2;
		dec.pos += seg_total;

		switch (marker) {
		case 0xDB: /* DQT */
			if (!parse_dqt(&dec, seg, seg_len)) goto fail;
			break;
		case 0xC4: /* DHT */
			if (!parse_dht(&dec, seg, seg_len)) goto fail;
			break;
		case 0xC0: /* SOF0 (Baseline DCT) */
			if (!parse_sof0(&dec, seg, seg_len)) goto fail;
			break;
		case 0xDD: /* DRI */
			if (seg_len >= 2) dec.restart_interval = rd_be16(seg);
			break;
		case 0xDA: /* SOS */
			if (!decode_sos(&dec, seg, seg_len, &rgba)) goto fail;
			decoded = 1;
			break;
		default:
			/* APPn, COM, etc. — skip */
			break;
		}
	}

	free(raw);
	if (!decoded || !rgba) return CM_MISS;

	uint8_t *data = NULL;
	unsigned ref = img_new(&data, filename, dec.width, dec.height, IMG_LOAD);
	memcpy(data, rgba, (size_t)dec.width * (size_t)dec.height * 4);
	free(rgba);
	lam_tex_upd(ref, 0, 0, dec.width, dec.height, data);
	return ref;

fail:
	free(raw);
	free(rgba);
	return CM_MISS;
}

static int jpegi_save(const char *filename, const uint8_t *data,
                      uint32_t w, uint32_t h)
{
	(void)filename; (void)data; (void)w; (void)h;
	return -1;
}

void jpeg_construct(void)
{
	img_be_load("jpeg", jpegi_load, jpegi_save);
	img_be_load("jpg",  jpegi_load, jpegi_save);
	img_be_load("JPEG", jpegi_load, jpegi_save);
	img_be_load("JPG",  jpegi_load, jpegi_save);
}

__attribute__((constructor))
static void construct(void)
{
	jpeg_construct();
}
