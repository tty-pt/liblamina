/*
 * ui-css.c — Runtime C CSS parser, selector matcher, custom properties,
 * and declaration cascade engine for liblamina.
 */

#include "./ui.h"
#include "../include/ttypt/lam-font.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Property bitmask helpers ───────────────────────────────────────────── */

enum {
	P_BG_COLOR = 0,
	P_BG_COLOR2,
	P_COLOR,
	P_BORDER_W,
	P_BORDER_C,
	P_BORDER_TW, P_BORDER_RW, P_BORDER_BW, P_BORDER_LW,
	P_BORDER_TC, P_BORDER_RC, P_BORDER_BC, P_BORDER_LC,
	P_RADIUS_TL, P_RADIUS_TR, P_RADIUS_BR, P_RADIUS_BL,
	P_PAD_T, P_PAD_R, P_PAD_B, P_PAD_L,
	P_MAR_T, P_MAR_R, P_MAR_B, P_MAR_L,
	P_POS,
	P_TOP, P_RIGHT, P_BOTTOM, P_LEFT,
	P_DISPLAY,
	P_FLEX_DIR, P_FLEX_GROW, P_FLEX_SHRINK, P_FLEX_BASIS, P_FLEX_WRAP,
	P_ALIGN_ITEMS, P_ALIGN_SELF, P_JUSTIFY,
	P_ROW_GAP, P_COL_GAP,
	P_WIDTH, P_HEIGHT,
	P_MIN_W, P_MAX_W, P_MIN_H, P_MAX_H,
	P_FONT_SIZE, P_FONT_WEIGHT, P_FONT_MONO, P_LINE_HEIGHT,
	P_TEXT_ALIGN, P_WHITE_SPACE, P_WORD_BREAK,
	P_OVERFLOW_X, P_OVERFLOW_Y,
	P_Z_INDEX, P_OPACITY, P_VISIBILITY, P_POINTER_EVENTS,
	P_BOX_SHADOW, P_BOX_SIZING, P_OBJECT_FIT,
	P_TRANSFORM, P_GRID_COLS, P_GRID_ROW, P_GRID_COL_SPAN,
	P_JUSTIFY_ITEMS, P_JUSTIFY_SELF,
	P_BG_IMAGE, P_BG_SIZE, P_BG_BLEND,
	P_TEXT_SHADOW, P_OUTLINE_W, P_OUTLINE_C,
	P_BACKDROP_BLUR, P_CURSOR,
	P_SVG_FILL, P_SVG_STROKE, P_SVG_STROKE_W,
	P_COUNT
};

static inline int mask_has(const uint64_t m[2], int id)
{
	return (m[id >> 6] & (1ULL << (id & 63))) != 0;
}

static inline void mask_set(uint64_t m[2], int id)
{
	m[id >> 6] |= (1ULL << (id & 63));
}

static inline int prop_can_write(const lam_ui_style_t *s, int id, int imp)
{
	if (mask_has(s->imp_mask, id) && !imp)
		return 0;
	return 1;
}

static inline void prop_mark(lam_ui_style_t *s, int id, int imp)
{
	mask_set(s->set_mask, id);
	if (imp)
		mask_set(s->imp_mask, id);
}

/* ── Media condition flags ──────────────────────────────────────────────── */

enum {
	M_NONE     = 0,
	M_DARK     = (1u << 0),
	M_LIGHT    = (1u << 1),
	M_MAX_W    = (1u << 2),
	M_MIN_W    = (1u << 3),
	M_PORTRAIT = (1u << 4),
	M_NEVER    = (1u << 5),
};

typedef struct {
	uint8_t  flags;
	uint16_t max_w;
	uint16_t min_w;
} css_media_t;

static int media_matches(css_media_t m)
{
	if (m.flags & M_NEVER)
		return 0;
	if ((m.flags & M_DARK) && !lam_ui_dark_mode)
		return 0;
	if ((m.flags & M_LIGHT) && lam_ui_dark_mode)
		return 0;
	if ((m.flags & M_MAX_W) && lam_ui_screen_w > m.max_w)
		return 0;
	if ((m.flags & M_MIN_W) && lam_ui_screen_w < m.min_w)
		return 0;
	if ((m.flags & M_PORTRAIT) && lam_ui_screen_h <= lam_ui_screen_w)
		return 0;
	return 1;
}

/* ── CSS AST structures ─────────────────────────────────────────────────── */

typedef struct {
	char   *name;
	char   *val;
	uint8_t op; /* 0=exists, '=' exact, '*' substr, '^' prefix, '$' suffix */
} css_attr_t;

typedef struct css_compound {
	char   *tag;
	char   *id;
	char  **classes;
	uint8_t n_classes;
	css_attr_t *attrs;
	uint8_t n_attrs;
	uint8_t req_pseudo;
	uint8_t is_root;
	uint8_t is_first_child;
	uint8_t is_last_child;
	uint8_t is_empty;
	uint8_t is_pseudo_el;
	struct css_compound *nots;
	uint8_t n_nots;
	struct css_compound *has_sel;
	uint8_t has_direct_child;
} css_compound_t;

#define CSS_MAX_PARTS 8

typedef struct {
	css_compound_t parts[CSS_MAX_PARTS];
	char           combs[CSS_MAX_PARTS]; /* ' ', '>', '+', '~' */
	uint8_t        n_parts;
	uint32_t       spec;
	uint32_t       order;
	uint32_t       block_idx;
	css_media_t    media;
} css_rule_t;

typedef struct {
	char   *prop;
	char   *val;
	uint8_t important;
} css_decl_t;

typedef struct {
	css_decl_t *decls;
	uint32_t    n_decls;
} css_block_t;

typedef struct {
	char       *name;
	char       *val;
	css_media_t media;
} css_var_t;

typedef struct css_sheet {
	uint32_t    ss_id;
	css_rule_t *rules;
	uint32_t    n_rules, cap_rules;
	css_block_t *blocks;
	uint32_t    n_blocks, cap_blocks;
	css_var_t  *vars;
	uint32_t    n_vars, cap_vars;
	uint32_t    next_order;
	struct css_sheet *next;
} css_sheet_t;

static css_sheet_t *g_sheets = NULL;

static css_sheet_t *sheet_find(uint32_t ss_id)
{
	for (css_sheet_t *s = g_sheets; s; s = s->next)
		if (s->ss_id == ss_id)
			return s;
	return NULL;
}

static css_sheet_t *sheet_get_or_create(uint32_t ss_id)
{
	css_sheet_t *s = sheet_find(ss_id);
	if (s)
		return s;
	s = calloc(1, sizeof(*s));
	if (!s)
		return NULL;
	s->ss_id = ss_id;
	s->next = g_sheets;
	g_sheets = s;
	return s;
}

static void free_compound(css_compound_t *c)
{
	free(c->tag);
	free(c->id);
	for (uint8_t i = 0; i < c->n_classes; i++)
		free(c->classes[i]);
	free(c->classes);
	for (uint8_t i = 0; i < c->n_attrs; i++) {
		free(c->attrs[i].name);
		free(c->attrs[i].val);
	}
	free(c->attrs);
	for (uint8_t i = 0; i < c->n_nots; i++)
		free_compound(&c->nots[i]);
	free(c->nots);
	if (c->has_sel) {
		free_compound(c->has_sel);
		free(c->has_sel);
	}
}

void lam_css_sheet_free(uint32_t ss_id)
{
	css_sheet_t **pp = &g_sheets;
	while (*pp) {
		if ((*pp)->ss_id == ss_id) {
			css_sheet_t *s = *pp;
			*pp = s->next;
			for (uint32_t i = 0; i < s->n_rules; i++) {
				for (uint8_t j = 0; j < s->rules[i].n_parts; j++)
					free_compound(&s->rules[i].parts[j]);
			}
			free(s->rules);
			for (uint32_t i = 0; i < s->n_blocks; i++) {
				for (uint32_t j = 0; j < s->blocks[i].n_decls; j++) {
					free(s->blocks[i].decls[j].prop);
					free(s->blocks[i].decls[j].val);
				}
				free(s->blocks[i].decls);
			}
			free(s->blocks);
			for (uint32_t i = 0; i < s->n_vars; i++) {
				free(s->vars[i].name);
				free(s->vars[i].val);
			}
			free(s->vars);
			free(s);
			return;
		}
		pp = &(*pp)->next;
	}
}

/* ── String helpers ─────────────────────────────────────────────────────── */

static char *xstrndup(const char *s, size_t n)
{
	char *r = malloc(n + 1);
	if (!r) return NULL;
	memcpy(r, s, n);
	r[n] = '\0';
	return r;
}

static int ascii_strncasecmp(const char *a, const char *b, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		unsigned char ca = (unsigned char)tolower((unsigned char)a[i]);
		unsigned char cb = (unsigned char)tolower((unsigned char)b[i]);
		if (ca != cb) return (int)ca - (int)cb;
		if (ca == 0) return 0;
	}
	return 0;
}

static int ascii_strcasecmp(const char *a, const char *b)
{
	if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
	while (*a && *b) {
		int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
		if (d) return d;
		a++; b++;
	}
	return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static void trim_span(const char **s, const char **e)
{
	while (*s < *e && isspace((unsigned char)**s)) (*s)++;
	while (*e > *s && isspace((unsigned char)*(*e - 1))) (*e)--;
}

/* Unescape CSS identifier escapes like \. \/ \[ \] \# \: */
static char *unescape_ident(const char *s, size_t len)
{
	char *out = malloc(len + 1);
	if (!out) return NULL;
	size_t o = 0;
	for (size_t i = 0; i < len; i++) {
		if (s[i] == '\\' && i + 1 < len) {
			i++;
			out[o++] = s[i];
		} else {
			out[o++] = s[i];
		}
	}
	out[o] = '\0';
	return out;
}

/* ── Custom property lookup & var() expansion ───────────────────────────── */

static const char *sheet_lookup_var(const css_sheet_t *sh, const char *name)
{
	if (!sh || !name) return NULL;
	/* Scan backwards so later rules / dark overrides win */
	for (int i = (int)sh->n_vars - 1; i >= 0; i--) {
		if (!media_matches(sh->vars[i].media))
			continue;
		if (strcmp(sh->vars[i].name, name) == 0)
			return sh->vars[i].val;
	}
	return NULL;
}

static void expand_vars_rec(const css_sheet_t *sh, const char *in,
                            char *out, size_t cap, int depth)
{
	size_t o = 0;
	const char *p = in;

	if (depth > 8) {
		snprintf(out, cap, "%s", in);
		return;
	}

	while (*p && o + 1 < cap) {
		if (ascii_strncasecmp(p, "var(", 4) == 0) {
			const char *q = p + 4;
			int par = 1;
			const char *comma = NULL;
			while (*q && par > 0) {
				if (*q == '(') par++;
				else if (*q == ')') par--;
				else if (*q == ',' && par == 1 && !comma) comma = q;
				if (par > 0) q++;
			}
			if (par == 0) {
				const char *ns = p + 4;
				const char *ne = comma ? comma : q;
				trim_span(&ns, &ne);
				char vname[128];
				size_t nlen = (size_t)(ne - ns);
				if (nlen >= sizeof(vname)) nlen = sizeof(vname) - 1;
				memcpy(vname, ns, nlen);
				vname[nlen] = '\0';

				const char *resolved = sheet_lookup_var(sh, vname);
				char sub[512];
				if (resolved) {
					expand_vars_rec(sh, resolved, sub, sizeof(sub), depth + 1);
				} else if (comma) {
					const char *fs = comma + 1;
					const char *fe = q;
					trim_span(&fs, &fe);
					char fraw[512];
					size_t flen = (size_t)(fe - fs);
					if (flen >= sizeof(fraw)) flen = sizeof(fraw) - 1;
					memcpy(fraw, fs, flen);
					fraw[flen] = '\0';
					expand_vars_rec(sh, fraw, sub, sizeof(sub), depth + 1);
				} else {
					sub[0] = '\0';
				}
				size_t slen = strlen(sub);
				if (o + slen >= cap) slen = cap - 1 - o;
				memcpy(out + o, sub, slen);
				o += slen;
				p = q + 1;
				continue;
			}
		}
		out[o++] = *p++;
	}
	out[o] = '\0';
}

/* ── Color parser ───────────────────────────────────────────────────────── */

static int hex_val(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static uint8_t clamp_u8(float v)
{
	if (v < 0.0f) return 0;
	if (v > 255.0f) return 255;
	return (uint8_t)(v + 0.5f);
}

static int parse_color(const char *str, uint32_t *out)
{
	if (!str) return 0;
	while (isspace((unsigned char)*str)) str++;
	const char *end = str + strlen(str);
	while (end > str && isspace((unsigned char)end[-1])) end--;
	size_t len = (size_t)(end - str);
	if (len == 0) return 0;

	if (len == 11 && ascii_strncasecmp(str, "transparent", 11) == 0) {
		*out = 0x00000000u;
		return 1;
	}
	if (len == 4 && ascii_strncasecmp(str, "none", 4) == 0) {
		*out = 0x00000000u;
		return 1;
	}

	/* Named colors */
	static const struct { const char *name; uint32_t col; } named[] = {
		{ "white",   0xFFFFFFFFu }, { "black",   0xFF000000u },
		{ "red",     0xFFFF0000u }, { "green",   0xFF008000u },
		{ "blue",    0xFF0000FFu }, { "yellow",  0xFFFFFF00u },
		{ "cyan",    0xFF00FFFFu }, { "magenta", 0xFFFF00FFu },
		{ "gray",    0xFF808080u }, { "grey",    0xFF808080u },
		{ "orange",  0xFFFFA500u }, { "purple",  0xFF800080u },
		{ "gold",    0xFFFFD700u }, { "silver",  0xFFC0C0C0u },
		{ "lime",       0xFF00FF00u }, { "navy",       0xFF000080u },
		{ "teal",       0xFF008080u }, { "maroon",     0xFF800000u },
		{ "crimson",    0xFFDC143Cu }, { "coral",      0xFFFF7F50u },
		{ "whitesmoke", 0xFFF5F5F5u }, { "gainsboro",  0xFFDCDCDCu },
		{ "darkgray",   0xFFA9A9A9u }, { "darkgrey",   0xFFA9A9A9u },
		{ "lightgray",  0xFFD3D3D3u }, { "lightgrey",  0xFFD3D3D3u },
		{ "dimgray",    0xFF696969u }, { "dimgrey",    0xFF696969u },
	};
	for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
		if (strlen(named[i].name) == len &&
		    ascii_strncasecmp(str, named[i].name, len) == 0) {
			*out = named[i].col;
			return 1;
		}
	}

	/* #hex */
	if (str[0] == '#') {
		const char *h = str + 1;
		size_t hl = len - 1;
		if (hl == 3 || hl == 4) {
			int r = hex_val(h[0]), g = hex_val(h[1]), b = hex_val(h[2]);
			int a = (hl == 4) ? hex_val(h[3]) : 15;
			if (r < 0 || g < 0 || b < 0 || a < 0) return 0;
			*out = LAM_RGBA(r * 17, g * 17, b * 17, a * 17);
			return 1;
		}
		if (hl == 6 || hl == 8) {
			int r1 = hex_val(h[0]), r2 = hex_val(h[1]);
			int g1 = hex_val(h[2]), g2 = hex_val(h[3]);
			int b1 = hex_val(h[4]), b2 = hex_val(h[5]);
			int a1 = (hl == 8) ? hex_val(h[6]) : 15;
			int a2 = (hl == 8) ? hex_val(h[7]) : 15;
			if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 ||
			    b1 < 0 || b2 < 0 || a1 < 0 || a2 < 0) return 0;
			*out = LAM_RGBA(r1 * 16 + r2, g1 * 16 + g2,
			                b1 * 16 + b2, a1 * 16 + a2);
			return 1;
		}
		return 0;
	}

	/* rgb(...) / rgba(...) */
	if ((len > 4 && ascii_strncasecmp(str, "rgb(", 4) == 0) ||
	    (len > 5 && ascii_strncasecmp(str, "rgba(", 5) == 0)) {
		const char *p = strchr(str, '(');
		if (!p) return 0;
		p++;
		float vals[4] = { 0, 0, 0, 1.0f };
		int is_pct[4] = { 0, 0, 0, 0 };
		int nv = 0;
		while (*p && *p != ')' && nv < 4) {
			while (*p && (isspace((unsigned char)*p) || *p == ',' || *p == '/'))
				p++;
			if (*p == ')' || !*p) break;
			char *ep = NULL;
			float v = strtof(p, &ep);
			if (ep == p) break;
			p = ep;
			if (*p == '%') { is_pct[nv] = 1; p++; }
			vals[nv++] = v;
		}
		if (nv < 3) return 0;
		float r = is_pct[0] ? vals[0] * 2.55f : vals[0];
		float g = is_pct[1] ? vals[1] * 2.55f : vals[1];
		float b = is_pct[2] ? vals[2] * 2.55f : vals[2];
		float a = (nv >= 4) ? (is_pct[3] ? vals[3] * 2.55f : vals[3] * 255.0f) : 255.0f;
		*out = LAM_RGBA(clamp_u8(r), clamp_u8(g), clamp_u8(b), clamp_u8(a));
		return 1;
	}

	/* color-mix(in srgb, C1 [P1%], C2 [P2%]) */
	if (len > 10 && ascii_strncasecmp(str, "color-mix(", 10) == 0) {
		const char *p = str + 10;
		const char *cl = end;
		if (cl > p && cl[-1] == ')') cl--;
		/* split top-level commas */
		const char *segs[3] = { NULL, NULL, NULL };
		size_t slens[3] = { 0, 0, 0 };
		int nseg = 0, par = 0;
		const char *s0 = p;
		for (const char *q = p; q <= cl && nseg < 3; q++) {
			if (q == cl || (*q == ',' && par == 0)) {
				segs[nseg] = s0;
				slens[nseg] = (size_t)(q - s0);
				nseg++;
				s0 = q + 1;
			} else if (*q == '(') par++;
			else if (*q == ')') par--;
		}
		if (nseg < 3) return 0;

		uint32_t c1 = 0, c2 = 0;
		float p1 = -1.0f, p2 = -1.0f;

		for (int k = 0; k < 2; k++) {
			char buf[256];
			size_t l = slens[k + 1];
			if (l >= sizeof(buf)) l = sizeof(buf) - 1;
			memcpy(buf, segs[k + 1], l);
			buf[l] = '\0';
			/* Check trailing percentage */
			char *pct = strrchr(buf, '%');
			float pv = -1.0f;
			if (pct) {
				char *ns = pct;
				while (ns > buf && (isdigit((unsigned char)ns[-1]) || ns[-1] == '.'))
					ns--;
				if (ns < pct) {
					pv = strtof(ns, NULL);
					*ns = '\0';
				}
			}
			uint32_t col = 0;
			if (!parse_color(buf, &col)) return 0;
			if (k == 0) { c1 = col; p1 = pv; }
			else        { c2 = col; p2 = pv; }
		}
		if (p1 < 0 && p2 < 0) { p1 = 50.0f; p2 = 50.0f; }
		else if (p1 < 0) p1 = 100.0f - p2;
		else if (p2 < 0) p2 = 100.0f - p1;
		float w1 = p1 * 0.01f, w2 = p2 * 0.01f;
		float a1 = LAM_COLOR_A(c1) / 255.0f;
		float a2 = LAM_COLOR_A(c2) / 255.0f;
		float a = a1 * w1 + a2 * w2;
		float r = 0, g = 0, b = 0;
		if (a > 1e-4f) {
			r = (LAM_COLOR_R(c1) * a1 * w1 + LAM_COLOR_R(c2) * a2 * w2) / a;
			g = (LAM_COLOR_G(c1) * a1 * w1 + LAM_COLOR_G(c2) * a2 * w2) / a;
			b = (LAM_COLOR_B(c1) * a1 * w1 + LAM_COLOR_B(c2) * a2 * w2) / a;
		}
		*out = LAM_RGBA(clamp_u8(r), clamp_u8(g), clamp_u8(b), clamp_u8(a * 255.0f));
		return 1;
	}

	return 0;
}

/* ── Length parser ──────────────────────────────────────────────────────── */

static int32_t len_to_px_static(lam_len_t l, float em_px);

static int parse_len(const char *s, lam_len_t *out)
{
	while (isspace((unsigned char)*s)) s++;
	if (!*s) return 0;

	memset(out, 0, sizeof(*out));
	if (ascii_strncasecmp(s, "auto", 4) == 0 && (s[4] == '\0' || isspace((unsigned char)s[4]))) {
		out->unit = LAM_LEN_AUTO;
		return 1;
	}
	if (ascii_strncasecmp(s, "none", 4) == 0 && (s[4] == '\0' || isspace((unsigned char)s[4]))) {
		out->unit = LAM_LEN_AUTO;
		return 1;
	}
	if (ascii_strncasecmp(s, "max-content", 11) == 0 ||
	    ascii_strncasecmp(s, "fit-content", 11) == 0 ||
	    ascii_strncasecmp(s, "min-content", 11) == 0) {
		out->unit = LAM_LEN_AUTO;
		return 1;
	}
	if (ascii_strncasecmp(s, "thin", 4) == 0) {
		out->unit = LAM_LEN_PX; out->val = 1.0f; return 1;
	}
	if (ascii_strncasecmp(s, "medium", 6) == 0) {
		out->unit = LAM_LEN_PX; out->val = 3.0f; return 1;
	}
	if (ascii_strncasecmp(s, "thick", 5) == 0) {
		out->unit = LAM_LEN_PX; out->val = 5.0f; return 1;
	}

	/* min(A, B) / max(A, B) */
	if (ascii_strncasecmp(s, "min(", 4) == 0 || ascii_strncasecmp(s, "max(", 4) == 0) {
		int is_max = (s[0] == 'm' || s[0] == 'M') && (s[1] == 'a' || s[1] == 'A');
		const char *p = s + 4;
		int par = 1;
		const char *comma = NULL, *close = NULL;
		for (const char *q = p; *q && par > 0; q++) {
			if (*q == '(') par++;
			else if (*q == ')') {
				par--;
				if (par == 0) { close = q; break; }
			} else if (*q == ',' && par == 1 && !comma) {
				comma = q;
			}
		}
		if (comma && close) {
			char a_buf[128], b_buf[128];
			size_t al = (size_t)(comma - p);
			size_t bl = (size_t)(close - (comma + 1));
			if (al >= sizeof(a_buf)) al = sizeof(a_buf) - 1;
			if (bl >= sizeof(b_buf)) bl = sizeof(b_buf) - 1;
			memcpy(a_buf, p, al); a_buf[al] = '\0';
			memcpy(b_buf, comma + 1, bl); b_buf[bl] = '\0';
			lam_len_t la, lb;
			int oa = parse_len(a_buf, &la);
			int ob = parse_len(b_buf, &lb);
			if (oa && ob) {
				if (!is_max) {
					if (la.unit == LAM_LEN_PCT && la.val >= 99.0f && lb.unit != LAM_LEN_PCT) {
						*out = lb;
						return 1;
					}
					if (lb.unit == LAM_LEN_PCT && lb.val >= 99.0f && la.unit != LAM_LEN_PCT) {
						*out = la;
						return 1;
					}
				}
				if (la.unit != LAM_LEN_PCT && lb.unit != LAM_LEN_PCT) {
					int32_t pa = len_to_px_static(la, 16.0f);
					int32_t pb = len_to_px_static(lb, 16.0f);
					*out = is_max ? ((pa >= pb) ? la : lb)
					              : ((pa <= pb) ? la : lb);
					return 1;
				}
				*out = la;
				return 1;
			}
			if (oa) { *out = la; return 1; }
			if (ob) { *out = lb; return 1; }
		}
		return 0;
	}

	/* calc(A [+-] B) */
	if (ascii_strncasecmp(s, "calc(", 5) == 0) {
		const char *p = s + 5;
		while (isspace((unsigned char)*p)) p++;
		char *ep = NULL;
		float v1 = strtof(p, &ep);
		if (ep == p) return 0;
		p = ep;
		lam_len_unit_t u1 = LAM_LEN_PX;
		if (ascii_strncasecmp(p, "px", 2) == 0) { u1 = LAM_LEN_PX; p += 2; }
		else if (ascii_strncasecmp(p, "vw", 2) == 0) { u1 = LAM_LEN_VW; p += 2; }
		else if (ascii_strncasecmp(p, "vh", 2) == 0) { u1 = LAM_LEN_VH; p += 2; }
		else if (ascii_strncasecmp(p, "rem", 3) == 0) { u1 = LAM_LEN_REM; p += 3; }
		else if (ascii_strncasecmp(p, "em", 2) == 0) { u1 = LAM_LEN_EM; p += 2; }
		else if (*p == '%') { u1 = LAM_LEN_PCT; p++; }

		while (isspace((unsigned char)*p)) p++;
		while (*p == '*' || *p == '/') {
			char op = *p++;
			while (isspace((unsigned char)*p)) p++;
			float f = strtof(p, &ep);
			if (ep == p) break;
			p = ep;
			if (op == '*') v1 *= f;
			else if (f != 0.0f) v1 /= f;
			while (isspace((unsigned char)*p)) p++;
		}
		float sign = 1.0f;
		if (*p == '+') { sign = 1.0f; p++; }
		else if (*p == '-') { sign = -1.0f; p++; }
		else {
			out->unit = u1;
			out->val = v1;
			return 1;
		}
		while (isspace((unsigned char)*p)) p++;
		float v2 = strtof(p, &ep);
		if (ep == p) return 0;
		p = ep;
		float px2 = v2;
		if (ascii_strncasecmp(p, "rem", 3) == 0) px2 = v2 * 16.0f;
		else if (ascii_strncasecmp(p, "em", 2) == 0) px2 = v2 * 16.0f;
		else if (ascii_strncasecmp(p, "pt", 2) == 0) px2 = v2 * 1.3333f;

		if (u1 == LAM_LEN_PX) {
			out->unit = LAM_LEN_PX;
			out->val = v1 + sign * px2;
		} else {
			out->unit = LAM_LEN_CALC;
			out->val = v1;
			out->calc_unit = u1;
			out->calc_px = sign * px2;
		}
		return 1;
	}

	char *ep = NULL;
	float v = strtof(s, &ep);
	if (ep == s) return 0;

	if (ascii_strncasecmp(ep, "px", 2) == 0) {
		out->unit = LAM_LEN_PX; out->val = v;
	} else if (ascii_strncasecmp(ep, "rem", 3) == 0) {
		out->unit = LAM_LEN_REM; out->val = v;
	} else if (ascii_strncasecmp(ep, "em", 2) == 0) {
		out->unit = LAM_LEN_EM; out->val = v;
	} else if (ascii_strncasecmp(ep, "vw", 2) == 0) {
		out->unit = LAM_LEN_VW; out->val = v;
	} else if (ascii_strncasecmp(ep, "vh", 2) == 0) {
		out->unit = LAM_LEN_VH; out->val = v;
	} else if (ascii_strncasecmp(ep, "pt", 2) == 0) {
		out->unit = LAM_LEN_PX; out->val = v * 1.333333f;
	} else if (*ep == '%') {
		out->unit = LAM_LEN_PCT; out->val = v;
	} else {
		out->unit = LAM_LEN_PX; out->val = v;
	}
	return 1;
}

static int32_t len_to_px_static(lam_len_t l, float em_px)
{
	switch (l.unit) {
		case LAM_LEN_PX:  return (int32_t)lroundf(l.val);
		case LAM_LEN_REM: return (int32_t)lroundf(l.val * 16.0f);
		case LAM_LEN_EM:  return (int32_t)lroundf(l.val * em_px);
		case LAM_LEN_VW:  return (int32_t)lroundf(l.val * 0.01f * (float)lam_ui_screen_w);
		case LAM_LEN_VH:  return (int32_t)lroundf(l.val * 0.01f * (float)lam_ui_screen_h);
		case LAM_LEN_CALC: {
			lam_len_t base = { l.calc_unit, l.val, LAM_LEN_AUTO, 0 };
			return len_to_px_static(base, em_px) + (int32_t)lroundf(l.calc_px);
		}
		default: return 0;
	}
}

/* Split whitespace-separated tokens respecting (...) parentheses */
static int split_tokens(const char *s, char out[8][128], int max_tok)
{
	int n = 0;
	while (*s && n < max_tok) {
		while (isspace((unsigned char)*s)) s++;
		if (!*s) break;
		const char *t0 = s;
		int par = 0;
		while (*s && (par > 0 || !isspace((unsigned char)*s))) {
			if (*s == '(') par++;
			else if (*s == ')' && par > 0) par--;
			s++;
		}
		size_t l = (size_t)(s - t0);
		if (l >= 128) l = 127;
		memcpy(out[n], t0, l);
		out[n][l] = '\0';
		n++;
	}
	return n;
}

/* ── Declaration evaluator ──────────────────────────────────────────────── */

static int parse_overflow_val(const char *v, lam_ui_overflow_t *out)
{
	if (ascii_strcasecmp(v, "visible") == 0) { *out = LAM_UI_OVERFLOW_VISIBLE; return 1; }
	if (ascii_strcasecmp(v, "hidden")  == 0 || ascii_strcasecmp(v, "clip") == 0) {
		*out = LAM_UI_OVERFLOW_HIDDEN; return 1;
	}
	if (ascii_strcasecmp(v, "auto")    == 0) { *out = LAM_UI_OVERFLOW_AUTO; return 1; }
	if (ascii_strcasecmp(v, "scroll")  == 0) { *out = LAM_UI_OVERFLOW_SCROLL; return 1; }
	return 0;
}

static int parse_align_val(const char *v, lam_ui_align_mode_t *out)
{
	if (ascii_strcasecmp(v, "stretch") == 0) { *out = LAM_UI_ALIGN_STRETCH; return 1; }
	if (ascii_strcasecmp(v, "flex-start") == 0 || ascii_strcasecmp(v, "start") == 0) {
		*out = LAM_UI_ALIGN_FLEX_START; return 1;
	}
	if (ascii_strcasecmp(v, "center") == 0) { *out = LAM_UI_ALIGN_CENTER; return 1; }
	if (ascii_strcasecmp(v, "flex-end") == 0 || ascii_strcasecmp(v, "end") == 0) {
		*out = LAM_UI_ALIGN_FLEX_END; return 1;
	}
	if (ascii_strcasecmp(v, "auto") == 0) { *out = LAM_UI_ALIGN_AUTO; return 1; }
	return 0;
}

static void parse_gradient(const char *val, uint8_t *grad_type, float *angle,
                           uint32_t *c1, uint32_t *c2, int *got1, int *got2)
{
	*got1 = 0; *got2 = 0;
	*grad_type = (ascii_strncasecmp(val, "radial-gradient(", 16) == 0) ? 2 : 1;
	*angle = 180.0f;
	const char *p = strchr(val, '(');
	if (!p) return;
	p++;
	/* Scan comma-separated args inside gradient(...) */
	int par = 0;
	int seg_idx = 0;
	const char *s0 = p;
	for (const char *q = p; ; q++) {
		int is_end = 0;
		if (*q == '(') par++;
		else if (*q == ')') {
			if (par == 0) is_end = 1;
			else par--;
		} else if (*q == ',' && par == 0) {
			is_end = 2;
		} else if (!*q) {
			is_end = 1;
		}
		if (is_end) {
			const char *ts = s0, *te = q;
			trim_span(&ts, &te);
			char tok[128];
			size_t l = (size_t)(te - ts);
			if (l >= sizeof(tok)) l = sizeof(tok) - 1;
			memcpy(tok, ts, l);
			tok[l] = '\0';

			if (seg_idx == 0 && *grad_type == 1) {
				/* Optional angle or "to <side>" direction */
				if (l >= 3 && ascii_strncasecmp(tok + l - 3, "deg", 3) == 0) {
					*angle = strtof(tok, NULL);
					seg_idx++;
					if (is_end == 1) break;
					s0 = q + 1;
					continue;
				}
				if (ascii_strncasecmp(tok, "to ", 3) == 0) {
					int has_t = strstr(tok, "top") != NULL;
					int has_b = strstr(tok, "bottom") != NULL;
					int has_l = strstr(tok, "left") != NULL;
					int has_r = strstr(tok, "right") != NULL;
					if (has_t && has_r) *angle = 45.0f;
					else if (has_b && has_r) *angle = 135.0f;
					else if (has_b && has_l) *angle = 225.0f;
					else if (has_t && has_l) *angle = 315.0f;
					else if (has_t) *angle = 0.0f;
					else if (has_r) *angle = 90.0f;
					else if (has_b) *angle = 180.0f;
					else if (has_l) *angle = 270.0f;
					seg_idx++;
					if (is_end == 1) break;
					s0 = q + 1;
					continue;
				}
			}

			char parts[4][128];
			int nt = split_tokens(tok, parts, 4);
			for (int i = 0; i < nt; i++) {
				uint32_t col;
				if (parse_color(parts[i], &col)) {
					if (!*got1) { *c1 = col; *got1 = 1; }
					*c2 = col; *got2 = 1;
				}
			}
			seg_idx++;
			if (is_end == 1) break;
			s0 = q + 1;
		}
	}
}

#define CSS_URL_MISS_CAP 64
static char g_url_miss[CSS_URL_MISS_CAP][256];
static uint32_t g_url_miss_next = 0;

static uint32_t load_css_url_tex(const char *tok)
{
	const char *p = strchr(tok, '(');
	if (!p) return CM_MISS;
	p++;
	const char *end = strrchr(p, ')');
	if (!end || end <= p) return CM_MISS;
	trim_span(&p, &end);
	if (end - p >= 2 && ((*p == '"' && end[-1] == '"') || (*p == '\'' && end[-1] == '\''))) {
		p++;
		end--;
	}
	if (end <= p) return CM_MISS;
	if ((end - p) >= 5 && ascii_strncasecmp(p, "data:", 5) == 0)
		return CM_MISS;

	while (p < end && *p == '/') p++;
	char rel[256];
	size_t rl = (size_t)(end - p);
	if (rl == 0) return CM_MISS;
	if (rl >= sizeof(rel)) rl = sizeof(rel) - 1;
	memcpy(rel, p, rl);
	rel[rl] = '\0';

	const char *root = lam_ui_asset_root[0] ? lam_ui_asset_root : "htdocs";
	char full[768];
	snprintf(full, sizeof(full), "%s/%s", root, rel);

	for (uint32_t i = 0; i < CSS_URL_MISS_CAP; i++) {
		if (g_url_miss[i][0] && strcmp(g_url_miss[i], full) == 0)
			return CM_MISS;
	}

	unsigned ref = lam_tex_load(full);
	if (ref == CM_MISS) {
		snprintf(g_url_miss[g_url_miss_next % CSS_URL_MISS_CAP],
		         sizeof(g_url_miss[0]), "%s", full);
		g_url_miss_next++;
	}
	return (uint32_t)ref;
}

static void apply_decl(lam_ui_style_t *st, const css_sheet_t *sh,
                       const char *prop, const char *raw_val, int imp)
{
	char val[512];
	if (strstr(raw_val, "var(") || strstr(raw_val, "VAR("))
		expand_vars_rec(sh, raw_val, val, sizeof(val), 0);
	else {
		snprintf(val, sizeof(val), "%s", raw_val);
	}

	const char *vs = val, *ve = val + strlen(val);
	trim_span(&vs, &ve);
	if (vs >= ve) return;
	char vbuf[512];
	size_t vlen = (size_t)(ve - vs);
	if (vlen >= sizeof(vbuf)) vlen = sizeof(vbuf) - 1;
	memcpy(vbuf, vs, vlen);
	vbuf[vlen] = '\0';

	float em_px = st->font_size_is_px && st->font_size > 0 ? (float)st->font_size : 16.0f;

	/* ── display ── */
	if (ascii_strcasecmp(prop, "display") == 0) {
		if (!prop_can_write(st, P_DISPLAY, imp)) return;
		if (ascii_strcasecmp(vbuf, "none") == 0) st->display = LAM_UI_DISPLAY_NONE;
		else if (ascii_strcasecmp(vbuf, "flex") == 0) st->display = LAM_UI_DISPLAY_FLEX;
		else if (ascii_strcasecmp(vbuf, "inline-flex") == 0) st->display = LAM_UI_DISPLAY_INLINE_FLEX;
		else if (ascii_strcasecmp(vbuf, "grid") == 0 || ascii_strcasecmp(vbuf, "inline-grid") == 0)
			st->display = LAM_UI_DISPLAY_GRID;
		else if (ascii_strcasecmp(vbuf, "inline-block") == 0) st->display = LAM_UI_DISPLAY_INLINE_BLOCK;
		else if (ascii_strcasecmp(vbuf, "inline") == 0) st->display = LAM_UI_DISPLAY_INLINE;
		else if (ascii_strcasecmp(vbuf, "contents") == 0) st->display = LAM_UI_DISPLAY_CONTENTS;
		else if (ascii_strcasecmp(vbuf, "block") == 0) st->display = LAM_UI_DISPLAY_BLOCK;
		else return;
		prop_mark(st, P_DISPLAY, imp);
		return;
	}

	/* ── position ── */
	if (ascii_strcasecmp(prop, "position") == 0) {
		if (!prop_can_write(st, P_POS, imp)) return;
		if (ascii_strcasecmp(vbuf, "absolute") == 0) st->position = LAM_UI_POSITION_ABSOLUTE;
		else if (ascii_strcasecmp(vbuf, "fixed") == 0) st->position = LAM_UI_POSITION_FIXED;
		else if (ascii_strcasecmp(vbuf, "relative") == 0 || ascii_strcasecmp(vbuf, "sticky") == 0)
			st->position = LAM_UI_POSITION_RELATIVE;
		else if (ascii_strcasecmp(vbuf, "static") == 0) st->position = LAM_UI_POSITION_STATIC;
		else return;
		prop_mark(st, P_POS, imp);
		return;
	}

	/* ── background-color ── */
	if (ascii_strcasecmp(prop, "background-color") == 0) {
		if (!prop_can_write(st, P_BG_COLOR, imp)) return;
		uint32_t col = 0;
		if (parse_color(vbuf, &col)) {
			st->background_color = col;
			st->background_color2 = 0;
			st->bg_grad_type = 0;
			prop_mark(st, P_BG_COLOR, imp);
			prop_mark(st, P_BG_COLOR2, imp);
		}
		return;
	}

	/* ── background-image ── */
	if (ascii_strcasecmp(prop, "background-image") == 0) {
		if (ascii_strcasecmp(vbuf, "none") == 0) {
			if (prop_can_write(st, P_BG_IMAGE, imp)) {
				st->background_image_ref = CM_MISS;
				prop_mark(st, P_BG_IMAGE, imp);
			}
			if (prop_can_write(st, P_BG_COLOR2, imp)) {
				st->background_color2 = 0;
				st->bg_grad_type = 0;
				prop_mark(st, P_BG_COLOR2, imp);
			}
			return;
		}
		if (ascii_strncasecmp(vbuf, "linear-gradient(", 16) == 0 ||
		    ascii_strncasecmp(vbuf, "radial-gradient(", 16) == 0) {
			if (!prop_can_write(st, P_BG_COLOR, imp)) return;
			uint8_t gt = 1; float ang = 180.0f;
			uint32_t c1 = 0, c2 = 0; int g1 = 0, g2 = 0;
			parse_gradient(vbuf, &gt, &ang, &c1, &c2, &g1, &g2);
			if (g1) {
				st->background_color = c1;
				st->background_color2 = g2 ? c2 : c1;
				st->bg_grad_type = gt;
				st->bg_grad_angle = ang;
				prop_mark(st, P_BG_COLOR, imp);
				prop_mark(st, P_BG_COLOR2, imp);
			}
			return;
		}
		if (ascii_strncasecmp(vbuf, "url(", 4) == 0) {
			if (!prop_can_write(st, P_BG_IMAGE, imp)) return;
			uint32_t ref = load_css_url_tex(vbuf);
			if (ref != CM_MISS) {
				st->background_image_ref = ref;
				prop_mark(st, P_BG_IMAGE, imp);
			}
		}
		return;
	}

	/* ── background-size ── */
	if (ascii_strcasecmp(prop, "background-size") == 0) {
		if (!prop_can_write(st, P_BG_SIZE, imp)) return;
		if (ascii_strcasecmp(vbuf, "cover") == 0) st->bg_size_mode = 1;
		else if (ascii_strcasecmp(vbuf, "contain") == 0) st->bg_size_mode = 2;
		else st->bg_size_mode = 0;
		prop_mark(st, P_BG_SIZE, imp);
		return;
	}

	/* ── background-blend-mode ── */
	if (ascii_strcasecmp(prop, "background-blend-mode") == 0) {
		if (!prop_can_write(st, P_BG_BLEND, imp)) return;
		if (ascii_strcasecmp(vbuf, "multiply") == 0) st->bg_blend_mode = 1;
		else if (ascii_strcasecmp(vbuf, "screen") == 0) st->bg_blend_mode = 2;
		else st->bg_blend_mode = 0;
		prop_mark(st, P_BG_BLEND, imp);
		return;
	}

	/* ── background shorthand ── */
	if (ascii_strcasecmp(prop, "background") == 0) {
		uint32_t col = 0;
		if (ascii_strncasecmp(vbuf, "linear-gradient(", 16) == 0 ||
		    ascii_strncasecmp(vbuf, "radial-gradient(", 16) == 0) {
			if (prop_can_write(st, P_BG_COLOR, imp)) {
				uint8_t gt = 1; float ang = 180.0f;
				uint32_t c1 = 0, c2 = 0; int g1 = 0, g2 = 0;
				parse_gradient(vbuf, &gt, &ang, &c1, &c2, &g1, &g2);
				if (g1) {
					st->background_color = c1;
					st->background_color2 = g2 ? c2 : c1;
					st->bg_grad_type = gt;
					st->bg_grad_angle = ang;
					prop_mark(st, P_BG_COLOR, imp);
					prop_mark(st, P_BG_COLOR2, imp);
				}
			}
			if (prop_can_write(st, P_BG_IMAGE, imp)) {
				st->background_image_ref = CM_MISS;
				prop_mark(st, P_BG_IMAGE, imp);
			}
			return;
		}
		if (parse_color(vbuf, &col)) {
			if (prop_can_write(st, P_BG_COLOR, imp)) {
				st->background_color = col;
				st->background_color2 = 0;
				st->bg_grad_type = 0;
				prop_mark(st, P_BG_COLOR, imp);
				prop_mark(st, P_BG_COLOR2, imp);
			}
			if (prop_can_write(st, P_BG_IMAGE, imp)) {
				st->background_image_ref = CM_MISS;
				prop_mark(st, P_BG_IMAGE, imp);
			}
			return;
		}
		/* Multi-token shorthand: url(...), pos/size, gradient, color */
		char toks[8][128];
		int nt = split_tokens(vbuf, toks, 8);
		int got_color = 0, got_grad = 0, got_url = 0;
		for (int i = 0; i < nt; i++) {
			if (ascii_strncasecmp(toks[i], "url(", 4) == 0) {
				got_url = 1;
				if (prop_can_write(st, P_BG_IMAGE, imp)) {
					uint32_t ref = load_css_url_tex(toks[i]);
					if (ref != CM_MISS) {
						st->background_image_ref = ref;
						prop_mark(st, P_BG_IMAGE, imp);
					}
				}
			} else if (ascii_strncasecmp(toks[i], "linear-gradient(", 16) == 0 ||
			           ascii_strncasecmp(toks[i], "radial-gradient(", 16) == 0) {
				got_grad = 1;
				if (prop_can_write(st, P_BG_COLOR, imp)) {
					uint8_t gt = 1; float ang = 180.0f;
					uint32_t c1 = 0, c2 = 0; int g1 = 0, g2 = 0;
					parse_gradient(toks[i], &gt, &ang, &c1, &c2, &g1, &g2);
					if (g1) {
						st->background_color = c1;
						st->background_color2 = g2 ? c2 : c1;
						st->bg_grad_type = gt;
						st->bg_grad_angle = ang;
						prop_mark(st, P_BG_COLOR, imp);
						prop_mark(st, P_BG_COLOR2, imp);
					}
				}
			} else if (strstr(toks[i], "/cover") || ascii_strcasecmp(toks[i], "cover") == 0) {
				if (prop_can_write(st, P_BG_SIZE, imp)) {
					st->bg_size_mode = 1;
					prop_mark(st, P_BG_SIZE, imp);
				}
			} else if (strstr(toks[i], "/contain") || ascii_strcasecmp(toks[i], "contain") == 0) {
				if (prop_can_write(st, P_BG_SIZE, imp)) {
					st->bg_size_mode = 2;
					prop_mark(st, P_BG_SIZE, imp);
				}
			} else if (parse_color(toks[i], &col)) {
				got_color = 1;
				if (prop_can_write(st, P_BG_COLOR, imp)) {
					st->background_color = col;
					st->background_color2 = 0;
					st->bg_grad_type = 0;
					prop_mark(st, P_BG_COLOR, imp);
					prop_mark(st, P_BG_COLOR2, imp);
				}
			}
		}
		if (got_url && !got_color && !got_grad && prop_can_write(st, P_BG_COLOR, imp)) {
			st->background_color = 0;
			st->background_color2 = 0;
			st->bg_grad_type = 0;
			prop_mark(st, P_BG_COLOR, imp);
			prop_mark(st, P_BG_COLOR2, imp);
		}
		if (!got_url && (got_color || got_grad) && prop_can_write(st, P_BG_IMAGE, imp)) {
			st->background_image_ref = CM_MISS;
			prop_mark(st, P_BG_IMAGE, imp);
		}
		return;
	}

	/* ── color ── */
	if (ascii_strcasecmp(prop, "color") == 0) {
		if (!prop_can_write(st, P_COLOR, imp)) return;
		uint32_t col = 0;
		if (parse_color(vbuf, &col)) {
			st->color = col;
			prop_mark(st, P_COLOR, imp);
		}
		return;
	}

	/* ── width / height / min / max ── */
	if (ascii_strcasecmp(prop, "width") == 0) {
		if (!prop_can_write(st, P_WIDTH, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			st->width_len = l;
			st->width = (l.unit == LAM_LEN_PX) ? (int32_t)lroundf(l.val) : LAM_UI_AUTO;
			prop_mark(st, P_WIDTH, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "height") == 0) {
		if (!prop_can_write(st, P_HEIGHT, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			st->height_len = l;
			st->height = (l.unit == LAM_LEN_PX) ? (int32_t)lroundf(l.val) : LAM_UI_AUTO;
			prop_mark(st, P_HEIGHT, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "min-width") == 0) {
		if (!prop_can_write(st, P_MIN_W, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) { st->min_width = l; prop_mark(st, P_MIN_W, imp); }
		return;
	}
	if (ascii_strcasecmp(prop, "max-width") == 0) {
		if (!prop_can_write(st, P_MAX_W, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) { st->max_width = l; prop_mark(st, P_MAX_W, imp); }
		return;
	}
	if (ascii_strcasecmp(prop, "min-height") == 0) {
		if (!prop_can_write(st, P_MIN_H, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) { st->min_height = l; prop_mark(st, P_MIN_H, imp); }
		return;
	}
	if (ascii_strcasecmp(prop, "max-height") == 0) {
		if (!prop_can_write(st, P_MAX_H, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) { st->max_height = l; prop_mark(st, P_MAX_H, imp); }
		return;
	}

	/* ── top / right / bottom / left / inset ── */
	if (ascii_strcasecmp(prop, "top") == 0) {
		if (!prop_can_write(st, P_TOP, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			st->top_len = l;
			st->top = (l.unit == LAM_LEN_PX) ? (int32_t)lroundf(l.val) : LAM_UI_AUTO;
			prop_mark(st, P_TOP, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "right") == 0) {
		if (!prop_can_write(st, P_RIGHT, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			st->right_len = l;
			st->right = (l.unit == LAM_LEN_PX) ? (int32_t)lroundf(l.val) : LAM_UI_AUTO;
			prop_mark(st, P_RIGHT, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "bottom") == 0) {
		if (!prop_can_write(st, P_BOTTOM, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			st->bottom_len = l;
			st->bottom = (l.unit == LAM_LEN_PX) ? (int32_t)lroundf(l.val) : LAM_UI_AUTO;
			prop_mark(st, P_BOTTOM, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "left") == 0) {
		if (!prop_can_write(st, P_LEFT, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			st->left_len = l;
			st->left = (l.unit == LAM_LEN_PX) ? (int32_t)lroundf(l.val) : LAM_UI_AUTO;
			prop_mark(st, P_LEFT, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "inset") == 0) {
		char toks[8][128];
		int nt = split_tokens(vbuf, toks, 4);
		if (nt >= 1) {
			const char *t = toks[0];
			const char *r = nt >= 2 ? toks[1] : t;
			const char *b = nt >= 3 ? toks[2] : t;
			const char *l = nt >= 4 ? toks[3] : r;
			apply_decl(st, sh, "top", t, imp);
			apply_decl(st, sh, "right", r, imp);
			apply_decl(st, sh, "bottom", b, imp);
			apply_decl(st, sh, "left", l, imp);
		}
		return;
	}

	/* ── padding ── */
	if (ascii_strcasecmp(prop, "padding") == 0) {
		char toks[8][128];
		int nt = split_tokens(vbuf, toks, 4);
		if (nt >= 1) {
			const char *t = toks[0];
			const char *r = nt >= 2 ? toks[1] : t;
			const char *b = nt >= 3 ? toks[2] : t;
			const char *l = nt >= 4 ? toks[3] : r;
			apply_decl(st, sh, "padding-top", t, imp);
			apply_decl(st, sh, "padding-right", r, imp);
			apply_decl(st, sh, "padding-bottom", b, imp);
			apply_decl(st, sh, "padding-left", l, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "padding-top") == 0) {
		if (!prop_can_write(st, P_PAD_T, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			int32_t px = len_to_px_static(l, em_px);
			st->padding_top = px > 0 ? (uint32_t)px : 0;
			prop_mark(st, P_PAD_T, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "padding-right") == 0) {
		if (!prop_can_write(st, P_PAD_R, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			int32_t px = len_to_px_static(l, em_px);
			st->padding_right = px > 0 ? (uint32_t)px : 0;
			prop_mark(st, P_PAD_R, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "padding-bottom") == 0) {
		if (!prop_can_write(st, P_PAD_B, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			int32_t px = len_to_px_static(l, em_px);
			st->padding_bottom = px > 0 ? (uint32_t)px : 0;
			prop_mark(st, P_PAD_B, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "padding-left") == 0) {
		if (!prop_can_write(st, P_PAD_L, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			int32_t px = len_to_px_static(l, em_px);
			st->padding_left = px > 0 ? (uint32_t)px : 0;
			prop_mark(st, P_PAD_L, imp);
		}
		return;
	}

	/* ── margin ── */
	if (ascii_strcasecmp(prop, "margin") == 0) {
		char toks[8][128];
		int nt = split_tokens(vbuf, toks, 4);
		if (nt >= 1) {
			const char *t = toks[0];
			const char *r = nt >= 2 ? toks[1] : t;
			const char *b = nt >= 3 ? toks[2] : t;
			const char *l = nt >= 4 ? toks[3] : r;
			apply_decl(st, sh, "margin-top", t, imp);
			apply_decl(st, sh, "margin-right", r, imp);
			apply_decl(st, sh, "margin-bottom", b, imp);
			apply_decl(st, sh, "margin-left", l, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "margin-top") == 0) {
		if (!prop_can_write(st, P_MAR_T, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			st->margin_top_auto = (l.unit == LAM_LEN_AUTO);
			st->margin_top = st->margin_top_auto ? 0 : len_to_px_static(l, em_px);
			prop_mark(st, P_MAR_T, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "margin-right") == 0) {
		if (!prop_can_write(st, P_MAR_R, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			st->margin_right_auto = (l.unit == LAM_LEN_AUTO);
			st->margin_right = st->margin_right_auto ? 0 : len_to_px_static(l, em_px);
			prop_mark(st, P_MAR_R, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "margin-bottom") == 0) {
		if (!prop_can_write(st, P_MAR_B, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			st->margin_bottom_auto = (l.unit == LAM_LEN_AUTO);
			st->margin_bottom = st->margin_bottom_auto ? 0 : len_to_px_static(l, em_px);
			prop_mark(st, P_MAR_B, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "margin-left") == 0) {
		if (!prop_can_write(st, P_MAR_L, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			st->margin_left_auto = (l.unit == LAM_LEN_AUTO);
			st->margin_left = st->margin_left_auto ? 0 : len_to_px_static(l, em_px);
			prop_mark(st, P_MAR_L, imp);
		}
		return;
	}

	/* ── flex / flexbox ── */
	if (ascii_strcasecmp(prop, "flex-direction") == 0) {
		if (!prop_can_write(st, P_FLEX_DIR, imp)) return;
		if (ascii_strcasecmp(vbuf, "row") == 0) st->flex_direction = LAM_UI_ROW;
		else if (ascii_strcasecmp(vbuf, "column") == 0) st->flex_direction = LAM_UI_COLUMN;
		else if (ascii_strcasecmp(vbuf, "row-reverse") == 0) st->flex_direction = LAM_UI_ROW_REVERSE;
		else if (ascii_strcasecmp(vbuf, "column-reverse") == 0) st->flex_direction = LAM_UI_COLUMN_REVERSE;
		else return;
		prop_mark(st, P_FLEX_DIR, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "flex-wrap") == 0) {
		if (!prop_can_write(st, P_FLEX_WRAP, imp)) return;
		if (ascii_strcasecmp(vbuf, "wrap") == 0) st->flex_wrap = LAM_UI_WRAP_WRAP;
		else if (ascii_strcasecmp(vbuf, "nowrap") == 0) st->flex_wrap = LAM_UI_WRAP_NOWRAP;
		else return;
		prop_mark(st, P_FLEX_WRAP, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "flex-grow") == 0) {
		if (!prop_can_write(st, P_FLEX_GROW, imp)) return;
		st->flex_grow = strtof(vbuf, NULL);
		prop_mark(st, P_FLEX_GROW, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "flex-shrink") == 0) {
		if (!prop_can_write(st, P_FLEX_SHRINK, imp)) return;
		st->flex_shrink = strtof(vbuf, NULL);
		prop_mark(st, P_FLEX_SHRINK, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "flex-basis") == 0) {
		if (!prop_can_write(st, P_FLEX_BASIS, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			st->flex_basis_len = l;
			st->flex_basis = (l.unit == LAM_LEN_PX) ? (int32_t)lroundf(l.val) : LAM_UI_AUTO;
			prop_mark(st, P_FLEX_BASIS, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "flex") == 0) {
		if (ascii_strcasecmp(vbuf, "none") == 0) {
			apply_decl(st, sh, "flex-grow", "0", imp);
			apply_decl(st, sh, "flex-shrink", "0", imp);
			apply_decl(st, sh, "flex-basis", "auto", imp);
			return;
		}
		if (ascii_strcasecmp(vbuf, "auto") == 0) {
			apply_decl(st, sh, "flex-grow", "1", imp);
			apply_decl(st, sh, "flex-shrink", "1", imp);
			apply_decl(st, sh, "flex-basis", "auto", imp);
			return;
		}
		char toks[8][128];
		int nt = split_tokens(vbuf, toks, 3);
		if (nt == 1) {
			apply_decl(st, sh, "flex-grow", toks[0], imp);
			apply_decl(st, sh, "flex-shrink", "1", imp);
			apply_decl(st, sh, "flex-basis", "0px", imp);
		} else if (nt == 2) {
			apply_decl(st, sh, "flex-grow", toks[0], imp);
			/* 2nd could be shrink (unitless) or basis (length) */
			if (strchr(toks[1], 'p') || strchr(toks[1], '%') ||
			    strchr(toks[1], 'v') || strchr(toks[1], 'e') ||
			    ascii_strcasecmp(toks[1], "auto") == 0) {
				apply_decl(st, sh, "flex-shrink", "1", imp);
				apply_decl(st, sh, "flex-basis", toks[1], imp);
			} else {
				apply_decl(st, sh, "flex-shrink", toks[1], imp);
				apply_decl(st, sh, "flex-basis", "0px", imp);
			}
		} else if (nt >= 3) {
			apply_decl(st, sh, "flex-grow", toks[0], imp);
			apply_decl(st, sh, "flex-shrink", toks[1], imp);
			apply_decl(st, sh, "flex-basis", toks[2], imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "align-items") == 0) {
		if (!prop_can_write(st, P_ALIGN_ITEMS, imp)) return;
		lam_ui_align_mode_t m;
		if (parse_align_val(vbuf, &m)) {
			st->align_items = m;
			prop_mark(st, P_ALIGN_ITEMS, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "align-self") == 0) {
		if (!prop_can_write(st, P_ALIGN_SELF, imp)) return;
		lam_ui_align_mode_t m;
		if (parse_align_val(vbuf, &m)) {
			st->align_self = m;
			prop_mark(st, P_ALIGN_SELF, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "justify-content") == 0) {
		if (!prop_can_write(st, P_JUSTIFY, imp)) return;
		if (ascii_strcasecmp(vbuf, "flex-start") == 0 || ascii_strcasecmp(vbuf, "start") == 0)
			st->justify_content = LAM_UI_JUSTIFY_FLEX_START;
		else if (ascii_strcasecmp(vbuf, "center") == 0)
			st->justify_content = LAM_UI_JUSTIFY_CENTER;
		else if (ascii_strcasecmp(vbuf, "flex-end") == 0 || ascii_strcasecmp(vbuf, "end") == 0)
			st->justify_content = LAM_UI_JUSTIFY_FLEX_END;
		else if (ascii_strcasecmp(vbuf, "space-between") == 0)
			st->justify_content = LAM_UI_JUSTIFY_SPACE_BETWEEN;
		else if (ascii_strcasecmp(vbuf, "space-around") == 0)
			st->justify_content = LAM_UI_JUSTIFY_SPACE_AROUND;
		else if (ascii_strcasecmp(vbuf, "space-evenly") == 0)
			st->justify_content = LAM_UI_JUSTIFY_SPACE_EVENLY;
		else return;
		prop_mark(st, P_JUSTIFY, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "justify-items") == 0) {
		if (!prop_can_write(st, P_JUSTIFY_ITEMS, imp)) return;
		lam_ui_align_mode_t m;
		if (parse_align_val(vbuf, &m)) {
			st->justify_items = m;
			prop_mark(st, P_JUSTIFY_ITEMS, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "justify-self") == 0) {
		if (!prop_can_write(st, P_JUSTIFY_SELF, imp)) return;
		lam_ui_align_mode_t m;
		if (parse_align_val(vbuf, &m)) {
			st->justify_self = m;
			prop_mark(st, P_JUSTIFY_SELF, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "place-items") == 0) {
		char toks[8][128];
		int nt = split_tokens(vbuf, toks, 2);
		if (nt >= 1) {
			apply_decl(st, sh, "align-items", toks[0], imp);
			apply_decl(st, sh, "justify-items", nt >= 2 ? toks[1] : toks[0], imp);
		}
		return;
	}

	/* ── gap / row-gap / column-gap ── */
	if (ascii_strcasecmp(prop, "gap") == 0) {
		char toks[8][128];
		int nt = split_tokens(vbuf, toks, 2);
		if (nt >= 1) {
			apply_decl(st, sh, "row-gap", toks[0], imp);
			apply_decl(st, sh, "column-gap", nt >= 2 ? toks[1] : toks[0], imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "row-gap") == 0) {
		if (!prop_can_write(st, P_ROW_GAP, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			int32_t px = len_to_px_static(l, em_px);
			st->row_gap = px > 0 ? (uint32_t)px : 0;
			prop_mark(st, P_ROW_GAP, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "column-gap") == 0) {
		if (!prop_can_write(st, P_COL_GAP, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			int32_t px = len_to_px_static(l, em_px);
			st->column_gap = px > 0 ? (uint32_t)px : 0;
			prop_mark(st, P_COL_GAP, imp);
		}
		return;
	}

	/* ── border / border-width / border-color / per-side ── */
	if (ascii_strcasecmp(prop, "border") == 0) {
		if (!prop_can_write(st, P_BORDER_W, imp)) return;
		if (ascii_strcasecmp(vbuf, "none") == 0 || strcmp(vbuf, "0") == 0) {
			st->border_width = 0;
			st->border_top_width = st->border_right_width =
				st->border_bottom_width = st->border_left_width = 0;
			prop_mark(st, P_BORDER_W, imp);
			return;
		}
		char toks[8][128];
		int nt = split_tokens(vbuf, toks, 4);
		uint32_t bw = 1;
		uint32_t bc = st->border_color;
		int got_c = 0;
		for (int i = 0; i < nt; i++) {
			uint32_t col;
			lam_len_t l;
			if (ascii_strcasecmp(toks[i], "solid") == 0 ||
			    ascii_strcasecmp(toks[i], "dashed") == 0 ||
			    ascii_strcasecmp(toks[i], "dotted") == 0)
				continue;
			if (parse_color(toks[i], &col)) { bc = col; got_c = 1; }
			else if (parse_len(toks[i], &l)) {
				int32_t px = len_to_px_static(l, em_px);
				bw = (px <= 0 && l.val > 0.0f) ? 1u : (px > 0 ? (uint32_t)px : 0u);
			}
		}
		st->border_width = bw;
		st->border_top_width = st->border_right_width =
			st->border_bottom_width = st->border_left_width = bw;
		prop_mark(st, P_BORDER_W, imp);
		if (got_c && prop_can_write(st, P_BORDER_C, imp)) {
			st->border_color = bc;
			st->border_top_color = st->border_right_color =
				st->border_bottom_color = st->border_left_color = bc;
			prop_mark(st, P_BORDER_C, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "outline") == 0) {
		if (!prop_can_write(st, P_OUTLINE_W, imp)) return;
		if (ascii_strcasecmp(vbuf, "none") == 0 || strcmp(vbuf, "0") == 0) {
			st->outline_width = 0;
			prop_mark(st, P_OUTLINE_W, imp);
			return;
		}
		char toks[8][128];
		int nt = split_tokens(vbuf, toks, 4);
		uint8_t ow = 1;
		uint32_t oc = st->outline_color ? st->outline_color : st->color;
		int got_c = 0;
		for (int i = 0; i < nt; i++) {
			uint32_t col;
			lam_len_t l;
			if (ascii_strcasecmp(toks[i], "solid") == 0 ||
			    ascii_strcasecmp(toks[i], "dashed") == 0 ||
			    ascii_strcasecmp(toks[i], "dotted") == 0)
				continue;
			if (parse_color(toks[i], &col)) { oc = col; got_c = 1; }
			else if (parse_len(toks[i], &l)) {
				int32_t px = len_to_px_static(l, em_px);
				if (px <= 0 && l.val > 0.0f) ow = 1;
				else if (px > 255) ow = 255;
				else ow = px > 0 ? (uint8_t)px : 0;
			}
		}
		st->outline_width = ow;
		prop_mark(st, P_OUTLINE_W, imp);
		if (got_c && prop_can_write(st, P_OUTLINE_C, imp)) {
			st->outline_color = oc;
			prop_mark(st, P_OUTLINE_C, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "outline-width") == 0) {
		if (!prop_can_write(st, P_OUTLINE_W, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			int32_t px = len_to_px_static(l, em_px);
			st->outline_width = (px <= 0 && l.val > 0.0f) ? 1 : (px > 255 ? 255 : (px > 0 ? (uint8_t)px : 0));
			prop_mark(st, P_OUTLINE_W, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "outline-color") == 0) {
		if (!prop_can_write(st, P_OUTLINE_C, imp)) return;
		uint32_t col;
		if (parse_color(vbuf, &col)) {
			st->outline_color = col;
			prop_mark(st, P_OUTLINE_C, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "border-top") == 0 ||
	    ascii_strcasecmp(prop, "border-right") == 0 ||
	    ascii_strcasecmp(prop, "border-bottom") == 0 ||
	    ascii_strcasecmp(prop, "border-left") == 0) {
		int wp = P_BORDER_TW, cp = P_BORDER_TC;
		uint32_t *wptr = &st->border_top_width, *cptr = &st->border_top_color;
		if (ascii_strcasecmp(prop, "border-right") == 0) {
			wp = P_BORDER_RW; cp = P_BORDER_RC;
			wptr = &st->border_right_width; cptr = &st->border_right_color;
		} else if (ascii_strcasecmp(prop, "border-bottom") == 0) {
			wp = P_BORDER_BW; cp = P_BORDER_BC;
			wptr = &st->border_bottom_width; cptr = &st->border_bottom_color;
		} else if (ascii_strcasecmp(prop, "border-left") == 0) {
			wp = P_BORDER_LW; cp = P_BORDER_LC;
			wptr = &st->border_left_width; cptr = &st->border_left_color;
		}
		if (!prop_can_write(st, wp, imp)) return;
		if (ascii_strcasecmp(vbuf, "none") == 0 || strcmp(vbuf, "0") == 0) {
			*wptr = 0; prop_mark(st, wp, imp); return;
		}
		char toks[8][128];
		int nt = split_tokens(vbuf, toks, 4);
		uint32_t bw = 1;
		for (int i = 0; i < nt; i++) {
			uint32_t col;
			lam_len_t l;
			if (ascii_strcasecmp(toks[i], "solid") == 0) continue;
			if (parse_color(toks[i], &col)) {
				if (prop_can_write(st, cp, imp)) { *cptr = col; prop_mark(st, cp, imp); }
			} else if (parse_len(toks[i], &l)) {
				int32_t px = len_to_px_static(l, em_px);
				bw = px > 0 ? (uint32_t)px : 0;
			}
		}
		*wptr = bw;
		prop_mark(st, wp, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "border-width") == 0) {
		if (!prop_can_write(st, P_BORDER_W, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			int32_t px = len_to_px_static(l, em_px);
			st->border_width = px > 0 ? (uint32_t)px : 0;
			prop_mark(st, P_BORDER_W, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "border-color") == 0) {
		if (!prop_can_write(st, P_BORDER_C, imp)) return;
		uint32_t col;
		if (parse_color(vbuf, &col)) {
			st->border_color = col;
			prop_mark(st, P_BORDER_C, imp);
		}
		return;
	}

	/* ── border-radius ── */
	if (ascii_strcasecmp(prop, "border-radius") == 0) {
		char toks[8][128];
		int nt = split_tokens(vbuf, toks, 4);
		if (nt >= 1) {
			uint32_t rv[4] = { 0, 0, 0, 0 };
			int is_pct = 0;
			for (int i = 0; i < nt; i++) {
				lam_len_t l;
				if (parse_len(toks[i], &l)) {
					if (l.unit == LAM_LEN_PCT && l.val >= 45.0f) {
						rv[i] = 9999;
						is_pct = 1;
					} else {
						int32_t px = len_to_px_static(l, em_px);
						rv[i] = px > 0 ? (uint32_t)px : 0;
					}
				}
			}
			uint32_t tl = rv[0];
			uint32_t tr = nt >= 2 ? rv[1] : tl;
			uint32_t br = nt >= 3 ? rv[2] : tl;
			uint32_t bl = nt >= 4 ? rv[3] : tr;
			if (prop_can_write(st, P_RADIUS_TL, imp)) { st->border_radius_top_left = tl; prop_mark(st, P_RADIUS_TL, imp); }
			if (prop_can_write(st, P_RADIUS_TR, imp)) { st->border_radius_top_right = tr; prop_mark(st, P_RADIUS_TR, imp); }
			if (prop_can_write(st, P_RADIUS_BR, imp)) { st->border_radius_bottom_right = br; prop_mark(st, P_RADIUS_BR, imp); }
			if (prop_can_write(st, P_RADIUS_BL, imp)) { st->border_radius_bottom_left = bl; prop_mark(st, P_RADIUS_BL, imp); }
			if (is_pct) st->border_radius_pct = 1;
		}
		return;
	}

	/* ── typography ── */
	if (ascii_strcasecmp(prop, "font-size") == 0) {
		if (!prop_can_write(st, P_FONT_SIZE, imp)) return;
		if (ascii_strcasecmp(vbuf, "inherit") == 0) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			int32_t px = len_to_px_static(l, em_px);
			if (px < 6) px = 6;
			if (px > 128) px = 128;
			st->font_size = (uint32_t)px;
			st->font_size_is_px = 1;
			prop_mark(st, P_FONT_SIZE, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "font-weight") == 0) {
		if (!prop_can_write(st, P_FONT_WEIGHT, imp)) return;
		if (ascii_strcasecmp(vbuf, "bold") == 0 || ascii_strcasecmp(vbuf, "bolder") == 0)
			st->font_weight = 700;
		else if (ascii_strcasecmp(vbuf, "normal") == 0)
			st->font_weight = 400;
		else {
			int w = atoi(vbuf);
			if (w >= 100 && w <= 900) st->font_weight = (uint16_t)w;
		}
		prop_mark(st, P_FONT_WEIGHT, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "font-family") == 0) {
		if (!prop_can_write(st, P_FONT_MONO, imp)) return;
		if (strstr(vbuf, "mono") || strstr(vbuf, "Mono") ||
		    strstr(vbuf, "Courier") || strstr(vbuf, "Fira Code") ||
		    strstr(vbuf, "Consolas") || strstr(vbuf, "Menlo"))
			st->font_mono = 1;
		else
			st->font_mono = 0;
		prop_mark(st, P_FONT_MONO, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "line-height") == 0) {
		if (!prop_can_write(st, P_LINE_HEIGHT, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l)) {
			if (strchr(vbuf, 'p') || strchr(vbuf, 'e') || strchr(vbuf, 'r'))
				st->line_height = (float)len_to_px_static(l, em_px);
			else
				st->line_height = l.val;
			prop_mark(st, P_LINE_HEIGHT, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "text-align") == 0) {
		if (!prop_can_write(st, P_TEXT_ALIGN, imp)) return;
		if (ascii_strcasecmp(vbuf, "center") == 0) st->text_align = LAM_UI_TEXT_ALIGN_CENTER;
		else if (ascii_strcasecmp(vbuf, "right") == 0 || ascii_strcasecmp(vbuf, "end") == 0)
			st->text_align = LAM_UI_TEXT_ALIGN_RIGHT;
		else if (ascii_strcasecmp(vbuf, "left") == 0 || ascii_strcasecmp(vbuf, "start") == 0)
			st->text_align = LAM_UI_TEXT_ALIGN_LEFT;
		else return;
		prop_mark(st, P_TEXT_ALIGN, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "white-space") == 0) {
		if (!prop_can_write(st, P_WHITE_SPACE, imp)) return;
		if (ascii_strcasecmp(vbuf, "normal") == 0) st->white_space = LAM_UI_WS_NORMAL;
		else if (ascii_strcasecmp(vbuf, "nowrap") == 0) st->white_space = LAM_UI_WS_NOWRAP;
		else if (ascii_strcasecmp(vbuf, "pre") == 0) st->white_space = LAM_UI_WS_PRE;
		else if (ascii_strcasecmp(vbuf, "pre-wrap") == 0) st->white_space = LAM_UI_WS_PRE_WRAP;
		else if (ascii_strcasecmp(vbuf, "pre-line") == 0) st->white_space = LAM_UI_WS_PRE_LINE;
		else return;
		prop_mark(st, P_WHITE_SPACE, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "word-break") == 0) {
		if (!prop_can_write(st, P_WORD_BREAK, imp)) return;
		if (ascii_strcasecmp(vbuf, "break-all") == 0) st->word_break = LAM_UI_WB_BREAK_ALL;
		else if (ascii_strcasecmp(vbuf, "break-word") == 0) st->word_break = LAM_UI_WB_BREAK_WORD;
		else if (ascii_strcasecmp(vbuf, "keep-all") == 0) st->word_break = LAM_UI_WB_KEEP_ALL;
		else st->word_break = LAM_UI_WB_NORMAL;
		prop_mark(st, P_WORD_BREAK, imp);
		return;
	}

	/* ── overflow ── */
	if (ascii_strcasecmp(prop, "overflow") == 0) {
		char toks[8][128];
		int nt = split_tokens(vbuf, toks, 2);
		if (nt >= 1) {
			apply_decl(st, sh, "overflow-x", toks[0], imp);
			apply_decl(st, sh, "overflow-y", nt >= 2 ? toks[1] : toks[0], imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "overflow-x") == 0) {
		if (!prop_can_write(st, P_OVERFLOW_X, imp)) return;
		lam_ui_overflow_t ov;
		if (parse_overflow_val(vbuf, &ov)) {
			st->overflow_x = ov;
			prop_mark(st, P_OVERFLOW_X, imp);
		}
		return;
	}
	if (ascii_strcasecmp(prop, "overflow-y") == 0) {
		if (!prop_can_write(st, P_OVERFLOW_Y, imp)) return;
		lam_ui_overflow_t ov;
		if (parse_overflow_val(vbuf, &ov)) {
			st->overflow_y = ov;
			prop_mark(st, P_OVERFLOW_Y, imp);
		}
		return;
	}

	/* ── z-index / opacity / visibility / pointer-events ── */
	if (ascii_strcasecmp(prop, "z-index") == 0) {
		if (!prop_can_write(st, P_Z_INDEX, imp)) return;
		if (ascii_strcasecmp(vbuf, "auto") == 0) {
			st->z_index = 0; st->z_index_set = 0;
		} else {
			st->z_index = atoi(vbuf); st->z_index_set = 1;
		}
		prop_mark(st, P_Z_INDEX, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "opacity") == 0) {
		if (!prop_can_write(st, P_OPACITY, imp)) return;
		float f = strtof(vbuf, NULL);
		if (f < 0.0f) f = 0.0f;
		if (f > 1.0f) f = 1.0f;
		st->opacity = f;
		prop_mark(st, P_OPACITY, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "visibility") == 0) {
		if (!prop_can_write(st, P_VISIBILITY, imp)) return;
		if (ascii_strcasecmp(vbuf, "hidden") == 0 || ascii_strcasecmp(vbuf, "collapse") == 0)
			st->visibility = LAM_UI_VIS_HIDDEN;
		else
			st->visibility = LAM_UI_VIS_VISIBLE;
		prop_mark(st, P_VISIBILITY, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "pointer-events") == 0) {
		if (!prop_can_write(st, P_POINTER_EVENTS, imp)) return;
		st->pointer_events = (ascii_strcasecmp(vbuf, "none") == 0)
			? LAM_UI_PE_NONE : LAM_UI_PE_AUTO;
		prop_mark(st, P_POINTER_EVENTS, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "cursor") == 0) {
		if (!prop_can_write(st, P_CURSOR, imp)) return;
		uint8_t cur = LAM_CURSOR_AUTO;
		if (ascii_strcasecmp(vbuf, "default") == 0) cur = LAM_CURSOR_DEFAULT;
		else if (ascii_strcasecmp(vbuf, "pointer") == 0) cur = LAM_CURSOR_POINTER;
		else if (ascii_strcasecmp(vbuf, "text") == 0) cur = LAM_CURSOR_TEXT;
		else if (ascii_strcasecmp(vbuf, "move") == 0 ||
		         ascii_strcasecmp(vbuf, "all-scroll") == 0) cur = LAM_CURSOR_MOVE;
		else if (ascii_strcasecmp(vbuf, "crosshair") == 0) cur = LAM_CURSOR_CROSSHAIR;
		else if (ascii_strcasecmp(vbuf, "ns-resize") == 0 ||
		         ascii_strcasecmp(vbuf, "n-resize") == 0 ||
		         ascii_strcasecmp(vbuf, "s-resize") == 0 ||
		         ascii_strcasecmp(vbuf, "row-resize") == 0) cur = LAM_CURSOR_NS_RESIZE;
		else if (ascii_strcasecmp(vbuf, "ew-resize") == 0 ||
		         ascii_strcasecmp(vbuf, "e-resize") == 0 ||
		         ascii_strcasecmp(vbuf, "w-resize") == 0 ||
		         ascii_strcasecmp(vbuf, "col-resize") == 0) cur = LAM_CURSOR_EW_RESIZE;
		else if (ascii_strcasecmp(vbuf, "nesw-resize") == 0 ||
		         ascii_strcasecmp(vbuf, "ne-resize") == 0 ||
		         ascii_strcasecmp(vbuf, "sw-resize") == 0) cur = LAM_CURSOR_NESW_RESIZE;
		else if (ascii_strcasecmp(vbuf, "nwse-resize") == 0 ||
		         ascii_strcasecmp(vbuf, "nw-resize") == 0 ||
		         ascii_strcasecmp(vbuf, "se-resize") == 0) cur = LAM_CURSOR_NWSE_RESIZE;
		else if (ascii_strcasecmp(vbuf, "grab") == 0) cur = LAM_CURSOR_GRAB;
		else if (ascii_strcasecmp(vbuf, "grabbing") == 0) cur = LAM_CURSOR_GRABBING;
		else if (ascii_strcasecmp(vbuf, "not-allowed") == 0 ||
		         ascii_strcasecmp(vbuf, "no-drop") == 0) cur = LAM_CURSOR_NOT_ALLOWED;
		st->cursor = cur;
		prop_mark(st, P_CURSOR, imp);
		return;
	}

	/* ── box-sizing / object-fit ── */
	if (ascii_strcasecmp(prop, "box-sizing") == 0) {
		if (!prop_can_write(st, P_BOX_SIZING, imp)) return;
		st->box_sizing = (ascii_strcasecmp(vbuf, "content-box") == 0)
			? LAM_UI_BOX_CONTENT : LAM_UI_BOX_BORDER;
		prop_mark(st, P_BOX_SIZING, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "object-fit") == 0) {
		if (!prop_can_write(st, P_OBJECT_FIT, imp)) return;
		if (ascii_strcasecmp(vbuf, "cover") == 0) st->object_fit = LAM_UI_FIT_COVER;
		else if (ascii_strcasecmp(vbuf, "contain") == 0) st->object_fit = LAM_UI_FIT_CONTAIN;
		else st->object_fit = LAM_UI_FIT_FILL;
		prop_mark(st, P_OBJECT_FIT, imp);
		return;
	}

	/* ── box-shadow ── */
	if (ascii_strcasecmp(prop, "box-shadow") == 0) {
		if (!prop_can_write(st, P_BOX_SHADOW, imp)) return;
		if (ascii_strcasecmp(vbuf, "none") == 0) {
			st->box_shadow_color = 0;
			st->box_shadow_blur = 0;
			st->box_shadow_offset_x = 0;
			st->box_shadow_offset_y = 0;
			prop_mark(st, P_BOX_SHADOW, imp);
			return;
		}
		/* Only take first shadow before top-level comma */
		char first[256];
		int par = 0;
		size_t fi = 0;
		for (const char *q = vbuf; *q && fi + 1 < sizeof(first); q++) {
			if (*q == '(') par++;
			else if (*q == ')' && par > 0) par--;
			else if (*q == ',' && par == 0) break;
			first[fi++] = *q;
		}
		first[fi] = '\0';
		char toks[8][128];
		int nt = split_tokens(first, toks, 6);
		float nums[4] = { 0, 0, 0, 0 };
		int nn = 0;
		int is_inset = 0;
		uint32_t col = 0x80000000u;
		for (int i = 0; i < nt; i++) {
			if (ascii_strcasecmp(toks[i], "inset") == 0) { is_inset = 1; continue; }
			uint32_t c;
			if (parse_color(toks[i], &c)) {
				col = c;
			} else if (nn < 4) {
				lam_len_t l;
				if (parse_len(toks[i], &l))
					nums[nn++] = (float)len_to_px_static(l, em_px);
			}
		}
		if (is_inset) {
			/* Common CSS pattern: inset 0 0 0 1px var(--accent) focus/active ring */
			if (nn >= 4 && nums[0] == 0.0f && nums[1] == 0.0f &&
			    nums[2] == 0.0f && nums[3] > 0.0f) {
				if (prop_can_write(st, P_BORDER_W, imp)) {
					st->border_width = (uint32_t)lroundf(nums[3]);
					prop_mark(st, P_BORDER_W, imp);
				}
				if (prop_can_write(st, P_BORDER_C, imp)) {
					st->border_color = col;
					prop_mark(st, P_BORDER_C, imp);
				}
			}
			return;
		}
		if (nn >= 2) {
			st->box_shadow_offset_x = nums[0];
			st->box_shadow_offset_y = nums[1];
			st->box_shadow_blur     = nn >= 3 ? nums[2] : 0.0f;
			st->box_shadow_color    = col;
			prop_mark(st, P_BOX_SHADOW, imp);
		}
		return;
	}

	/* ── text-shadow ── */
	if (ascii_strcasecmp(prop, "text-shadow") == 0) {
		if (!prop_can_write(st, P_TEXT_SHADOW, imp)) return;
		if (ascii_strcasecmp(vbuf, "none") == 0) {
			st->text_shadow_color = 0;
			st->text_shadow_x = 0;
			st->text_shadow_y = 0;
			prop_mark(st, P_TEXT_SHADOW, imp);
			return;
		}
		char first[256];
		int par = 0;
		size_t fi = 0;
		for (const char *q = vbuf; *q && fi + 1 < sizeof(first); q++) {
			if (*q == '(') par++;
			else if (*q == ')' && par > 0) par--;
			else if (*q == ',' && par == 0) break;
			first[fi++] = *q;
		}
		first[fi] = '\0';
		char toks[8][128];
		int nt = split_tokens(first, toks, 6);
		int32_t nums[3] = { 0, 0, 0 };
		int nn = 0;
		uint32_t col = 0xFF000000u;
		for (int i = 0; i < nt; i++) {
			uint32_t c;
			if (parse_color(toks[i], &c)) {
				col = c;
			} else if (nn < 3) {
				lam_len_t l;
				if (parse_len(toks[i], &l))
					nums[nn++] = len_to_px_static(l, em_px);
			}
		}
		if (nn >= 2) {
			int32_t sx = nums[0], sy = nums[1];
			if (sx < -127) sx = -127;
			if (sx > 127) sx = 127;
			if (sy < -127) sy = -127;
			if (sy > 127) sy = 127;
			st->text_shadow_x = (int8_t)sx;
			st->text_shadow_y = (int8_t)sy;
			st->text_shadow_color = col;
			prop_mark(st, P_TEXT_SHADOW, imp);
		}
		return;
	}

	/* ── backdrop-filter / -webkit-backdrop-filter ── */
	if (ascii_strcasecmp(prop, "backdrop-filter") == 0 ||
	    ascii_strcasecmp(prop, "-webkit-backdrop-filter") == 0) {
		if (!prop_can_write(st, P_BACKDROP_BLUR, imp)) return;
		if (ascii_strcasecmp(vbuf, "none") == 0) {
			st->backdrop_blur = 0.0f;
			prop_mark(st, P_BACKDROP_BLUR, imp);
			return;
		}
		for (const char *p = vbuf; *p; p++) {
			if (ascii_strncasecmp(p, "blur(", 5) == 0) {
				lam_len_t l;
				if (parse_len(p + 5, &l)) {
					int32_t px = len_to_px_static(l, em_px);
					st->backdrop_blur = px > 0 ? (float)px : 0.0f;
					prop_mark(st, P_BACKDROP_BLUR, imp);
				}
				break;
			}
		}
		return;
	}

	/* ── transform: translateX / translateY / translate ── */
	if (ascii_strcasecmp(prop, "transform") == 0) {
		if (!prop_can_write(st, P_TRANSFORM, imp)) return;
		st->transform_tx_px = st->transform_tx_pct = 0;
		st->transform_ty_px = st->transform_ty_pct = 0;
		if (ascii_strcasecmp(vbuf, "none") != 0) {
			const char *p = vbuf;
			while (*p) {
				if (ascii_strncasecmp(p, "translateX(", 11) == 0) {
					const char *a = p + 11;
					const char *close = a;
					int par = 1;
					while (*close && par > 0) {
						if (*close == '(') par++;
						else if (*close == ')') par--;
						if (par > 0) close++;
					}
					lam_len_t l;
					if (parse_len(a, &l)) {
						if (l.unit == LAM_LEN_PCT) {
							st->transform_tx_pct = l.val;
							st->transform_tx_px = 0;
						} else if (l.unit == LAM_LEN_CALC && l.calc_unit == LAM_LEN_PCT) {
							st->transform_tx_pct = l.val;
							st->transform_tx_px = l.calc_px;
						} else {
							st->transform_tx_px = (float)len_to_px_static(l, em_px);
							st->transform_tx_pct = 0;
						}
					}
					if (!*close) break;
					p = close;
				} else if (ascii_strncasecmp(p, "translateY(", 11) == 0) {
					const char *a = p + 11;
					const char *close = a;
					int par = 1;
					while (*close && par > 0) {
						if (*close == '(') par++;
						else if (*close == ')') par--;
						if (par > 0) close++;
					}
					lam_len_t l;
					if (parse_len(a, &l)) {
						if (l.unit == LAM_LEN_PCT) {
							st->transform_ty_pct = l.val;
							st->transform_ty_px = 0;
						} else if (l.unit == LAM_LEN_CALC && l.calc_unit == LAM_LEN_PCT) {
							st->transform_ty_pct = l.val;
							st->transform_ty_px = l.calc_px;
						} else {
							st->transform_ty_px = (float)len_to_px_static(l, em_px);
							st->transform_ty_pct = 0;
						}
					}
					if (!*close) break;
					p = close;
				} else if (ascii_strncasecmp(p, "translate(", 10) == 0) {
					const char *a = p + 10;
					const char *comma = NULL;
					const char *close = a;
					int par = 1;
					while (*close && par > 0) {
						if (*close == '(') par++;
						else if (*close == ')') par--;
						else if (*close == ',' && par == 1 && !comma) comma = close;
						if (par > 0) close++;
					}
					lam_len_t lx, ly;
					if (parse_len(a, &lx)) {
						if (lx.unit == LAM_LEN_PCT) {
							st->transform_tx_pct = lx.val;
							st->transform_tx_px = 0;
						} else if (lx.unit == LAM_LEN_CALC && lx.calc_unit == LAM_LEN_PCT) {
							st->transform_tx_pct = lx.val;
							st->transform_tx_px = lx.calc_px;
						} else {
							st->transform_tx_px = (float)len_to_px_static(lx, em_px);
							st->transform_tx_pct = 0;
						}
					}
					if (comma && comma < close && parse_len(comma + 1, &ly)) {
						if (ly.unit == LAM_LEN_PCT) {
							st->transform_ty_pct = ly.val;
							st->transform_ty_px = 0;
						} else if (ly.unit == LAM_LEN_CALC && ly.calc_unit == LAM_LEN_PCT) {
							st->transform_ty_pct = ly.val;
							st->transform_ty_px = ly.calc_px;
						} else {
							st->transform_ty_px = (float)len_to_px_static(ly, em_px);
							st->transform_ty_pct = 0;
						}
					}
					if (!*close) break;
					p = close;
				}
				p++;
			}
		}
		prop_mark(st, P_TRANSFORM, imp);
		return;
	}

	/* ── grid-template-columns / grid-auto-rows / grid-column ── */
	if (ascii_strcasecmp(prop, "grid-template-columns") == 0) {
		if (!prop_can_write(st, P_GRID_COLS, imp)) return;
		memset(st->grid_col_kind, 0, sizeof(st->grid_col_kind));
		memset(st->grid_col_val, 0, sizeof(st->grid_col_val));
		if (ascii_strncasecmp(vbuf, "repeat(", 7) == 0) {
			const char *p = vbuf + 7;
			int cnt = atoi(p);
			const char *comma = strchr(p, ',');
			uint32_t col_px = 0;
			uint8_t kind = 0; /* 0=fr, 1=px, 2=auto */
			float tval = 1.0f;
			if (comma) {
				comma++;
				while (isspace((unsigned char)*comma)) comma++;
				if (ascii_strncasecmp(comma, "auto", 4) == 0) {
					kind = 2;
				} else if (strstr(comma, "fr")) {
					kind = 0;
					float fv = strtof(comma, NULL);
					tval = fv > 0.0f ? fv : 1.0f;
				} else {
					lam_len_t l;
					if (parse_len(comma, &l)) {
						int32_t px = len_to_px_static(l, em_px);
						if (px > 0) {
							col_px = (uint32_t)px;
							kind = 1;
							tval = (float)px;
						}
					}
				}
			}
			if (cnt > 0) {
				st->grid_cols = (uint32_t)cnt;
				st->grid_col_px = col_px;
				int lim = cnt < LAM_UI_GRID_MAX_COLS ? cnt : LAM_UI_GRID_MAX_COLS;
				for (int i = 0; i < lim; i++) {
					st->grid_col_kind[i] = kind;
					st->grid_col_val[i] = tval;
				}
				prop_mark(st, P_GRID_COLS, imp);
			}
		} else {
			char toks[8][128];
			int nt = split_tokens(vbuf, toks, LAM_UI_GRID_MAX_COLS);
			if (nt > 0) {
				st->grid_cols = (uint32_t)nt;
				st->grid_col_px = 0;
				for (int i = 0; i < nt; i++) {
					const char *tk = toks[i];
					const char *fr = strstr(tk, "fr");
					if (ascii_strcasecmp(tk, "auto") == 0 ||
					    ascii_strcasecmp(tk, "min-content") == 0 ||
					    ascii_strcasecmp(tk, "max-content") == 0 ||
					    ascii_strncasecmp(tk, "fit-content(", 12) == 0) {
						st->grid_col_kind[i] = 2;
						st->grid_col_val[i] = 0.0f;
					} else if (fr) {
						st->grid_col_kind[i] = 0;
						float fv = strtof(tk, NULL);
						st->grid_col_val[i] = fv > 0.0f ? fv : 1.0f;
					} else {
						lam_len_t l;
						if (parse_len(tk, &l) && l.unit != LAM_LEN_AUTO) {
							int32_t px = len_to_px_static(l, em_px);
							if (px > 0) {
								st->grid_col_kind[i] = 1;
								st->grid_col_val[i] = (float)px;
							} else {
								st->grid_col_kind[i] = 0;
								st->grid_col_val[i] = 1.0f;
							}
						} else {
							st->grid_col_kind[i] = 2;
							st->grid_col_val[i] = 0.0f;
						}
					}
				}
				prop_mark(st, P_GRID_COLS, imp);
			}
		}
		return;
	}
	if (ascii_strcasecmp(prop, "grid-column") == 0) {
		if (!prop_can_write(st, P_GRID_COL_SPAN, imp)) return;
		if (strstr(vbuf, "-1") != NULL)
			st->grid_col_span_full = 1;
		else
			st->grid_col_span_full = 0;
		prop_mark(st, P_GRID_COL_SPAN, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "grid-auto-rows") == 0 ||
	    ascii_strcasecmp(prop, "grid-template-rows") == 0) {
		if (!prop_can_write(st, P_GRID_ROW, imp)) return;
		lam_len_t l;
		if (parse_len(vbuf, &l) && l.unit == LAM_LEN_PX && l.val > 0) {
			st->grid_row_px = (uint32_t)lroundf(l.val);
			prop_mark(st, P_GRID_ROW, imp);
		}
		return;
	}
	/* ── SVG presentation properties (fill / stroke / stroke-width) ── */
	if (ascii_strcasecmp(prop, "fill") == 0) {
		if (!prop_can_write(st, P_SVG_FILL, imp)) return;
		st->svg_fill = (ascii_strcasecmp(vbuf, "none") == 0 ||
		                ascii_strcasecmp(vbuf, "transparent") == 0) ? 0 : 1;
		prop_mark(st, P_SVG_FILL, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "stroke") == 0) {
		if (!prop_can_write(st, P_SVG_STROKE, imp)) return;
		st->svg_stroke = (ascii_strcasecmp(vbuf, "none") == 0 ||
		                  ascii_strcasecmp(vbuf, "transparent") == 0) ? 0 : 1;
		prop_mark(st, P_SVG_STROKE, imp);
		return;
	}
	if (ascii_strcasecmp(prop, "stroke-width") == 0) {
		if (!prop_can_write(st, P_SVG_STROKE_W, imp)) return;
		float sw = strtof(vbuf, NULL);
		if (sw > 0.0f) {
			st->svg_stroke_width = sw;
			prop_mark(st, P_SVG_STROKE_W, imp);
		}
		return;
	}
}

/* ── Selector parser ────────────────────────────────────────────────────── */

static int is_ident_char(unsigned char c)
{
	return isalnum(c) || c == '-' || c == '_';
}

static const char *parse_ident(const char *p, const char *end, char **out)
{
	const char *s = p;
	while (p < end) {
		if (*p == '\\' && p + 1 < end) {
			p += 2;
		} else if (is_ident_char((unsigned char)*p)) {
			p++;
		} else {
			break;
		}
	}
	if (p > s)
		*out = unescape_ident(s, (size_t)(p - s));
	else
		*out = NULL;
	return p;
}

static int parse_compound(const char **pp, const char *end,
                          css_compound_t *out, uint32_t *spec)
{
	const char *p = *pp;
	memset(out, 0, sizeof(*out));
	int matched_any = 0;

	while (p < end) {
		char c = *p;
		if (isspace((unsigned char)c) || c == '>' || c == '+' || c == '~' || c == ',')
			break;

		if (c == '*') {
			p++;
			matched_any = 1;
			continue;
		}
		if (c == '#') {
			p++;
			char *id = NULL;
			p = parse_ident(p, end, &id);
			if (!id) return 0;
			free(out->id);
			out->id = id;
			*spec += (1u << 16);
			matched_any = 1;
			continue;
		}
		if (c == '.') {
			p++;
			char *cls = NULL;
			p = parse_ident(p, end, &cls);
			if (!cls) return 0;
			out->classes = realloc(out->classes, (out->n_classes + 1) * sizeof(char *));
			out->classes[out->n_classes++] = cls;
			*spec += (1u << 8);
			matched_any = 1;
			continue;
		}
		if (c == '[') {
			p++;
			while (p < end && isspace((unsigned char)*p)) p++;
			char *aname = NULL;
			p = parse_ident(p, end, &aname);
			if (!aname) return 0;
			while (p < end && isspace((unsigned char)*p)) p++;
			uint8_t op = 0;
			char *aval = NULL;
			if (p < end && (*p == '=' || *p == '*' || *p == '^' || *p == '$')) {
				op = (uint8_t)*p++;
				if (op != '=' && p < end && *p == '=') p++;
				while (p < end && isspace((unsigned char)*p)) p++;
				if (p < end && (*p == '"' || *p == '\'')) {
					char q = *p++;
					const char *vs = p;
					while (p < end && *p != q) p++;
					aval = xstrndup(vs, (size_t)(p - vs));
					if (p < end && *p == q) p++;
				} else {
					const char *vs = p;
					while (p < end && *p != ']' && !isspace((unsigned char)*p)) p++;
					aval = xstrndup(vs, (size_t)(p - vs));
				}
			}
			while (p < end && *p != ']') p++;
			if (p < end && *p == ']') p++;
			out->attrs = realloc(out->attrs, (out->n_attrs + 1) * sizeof(css_attr_t));
			out->attrs[out->n_attrs].name = aname;
			out->attrs[out->n_attrs].val = aval;
			out->attrs[out->n_attrs].op = op;
			out->n_attrs++;
			*spec += (1u << 8);
			matched_any = 1;
			continue;
		}
		if (c == ':') {
			p++;
			if (p < end && *p == ':') {
				/* Pseudo-element ::before, ::after, etc. */
				p++;
				char *pe = NULL;
				p = parse_ident(p, end, &pe);
				free(pe);
				if (p < end && *p == '(') {
					int par = 1; p++;
					while (p < end && par > 0) {
						if (*p == '(') par++;
						else if (*p == ')') par--;
						p++;
					}
				}
				out->is_pseudo_el = 1;
				matched_any = 1;
				continue;
			}
			char *pname = NULL;
			p = parse_ident(p, end, &pname);
			if (!pname) return 0;

			const char *arg_s = NULL, *arg_e = NULL;
			if (p < end && *p == '(') {
				p++;
				arg_s = p;
				int par = 1;
				while (p < end && par > 0) {
					if (*p == '(') par++;
					else if (*p == ')') par--;
					if (par > 0) p++;
				}
				arg_e = p;
				if (p < end && *p == ')') p++;
			}

			if (ascii_strcasecmp(pname, "hover") == 0) {
				out->req_pseudo |= LAM_PSEUDO_HOVER;
				*spec += (1u << 8);
			} else if (ascii_strcasecmp(pname, "active") == 0) {
				out->req_pseudo |= LAM_PSEUDO_ACTIVE;
				*spec += (1u << 8);
			} else if (ascii_strcasecmp(pname, "focus") == 0 ||
			           ascii_strcasecmp(pname, "focus-visible") == 0) {
				out->req_pseudo |= LAM_PSEUDO_FOCUS;
				*spec += (1u << 8);
			} else if (ascii_strcasecmp(pname, "disabled") == 0) {
				out->req_pseudo |= LAM_PSEUDO_DISABLED;
				*spec += (1u << 8);
			} else if (ascii_strcasecmp(pname, "checked") == 0) {
				out->req_pseudo |= LAM_PSEUDO_CHECKED;
				*spec += (1u << 8);
			} else if (ascii_strcasecmp(pname, "root") == 0) {
				out->is_root = 1;
				*spec += (1u << 8);
			} else if (ascii_strcasecmp(pname, "first-child") == 0) {
				out->is_first_child = 1;
				*spec += (1u << 8);
			} else if (ascii_strcasecmp(pname, "last-child") == 0) {
				out->is_last_child = 1;
				*spec += (1u << 8);
			} else if (ascii_strcasecmp(pname, "empty") == 0) {
				out->is_empty = 1;
				*spec += (1u << 8);
			} else if (ascii_strcasecmp(pname, "before") == 0 ||
			           ascii_strcasecmp(pname, "after") == 0 ||
			           ascii_strcasecmp(pname, "placeholder") == 0) {
				out->is_pseudo_el = 1;
			} else if (ascii_strcasecmp(pname, "not") == 0 && arg_s && arg_e) {
				const char *ap = arg_s;
				trim_span(&ap, &arg_e);
				css_compound_t sub;
				uint32_t sub_spec = 0;
				if (parse_compound(&ap, arg_e, &sub, &sub_spec)) {
					out->nots = realloc(out->nots, (out->n_nots + 1) * sizeof(css_compound_t));
					out->nots[out->n_nots++] = sub;
					*spec += sub_spec;
				}
			} else if ((ascii_strcasecmp(pname, "where") == 0 ||
			            ascii_strcasecmp(pname, "is") == 0) && arg_s && arg_e) {
				const char *ap = arg_s;
				trim_span(&ap, &arg_e);
				/* Merge simple inner attr/class directly if single compound */
				css_compound_t sub;
				uint32_t sub_spec = 0;
				if (parse_compound(&ap, arg_e, &sub, &sub_spec)) {
					for (uint8_t k = 0; k < sub.n_attrs; k++) {
						out->attrs = realloc(out->attrs, (out->n_attrs + 1) * sizeof(css_attr_t));
						out->attrs[out->n_attrs++] = sub.attrs[k];
					}
					sub.n_attrs = 0;
					for (uint8_t k = 0; k < sub.n_classes; k++) {
						out->classes = realloc(out->classes, (out->n_classes + 1) * sizeof(char *));
						out->classes[out->n_classes++] = sub.classes[k];
					}
					sub.n_classes = 0;
					free_compound(&sub);
					if (ascii_strcasecmp(pname, "is") == 0)
						*spec += sub_spec;
				}
			} else if (ascii_strcasecmp(pname, "has") == 0 && arg_s && arg_e) {
				const char *ap = arg_s;
				trim_span(&ap, &arg_e);
				uint8_t direct = 0;
				if (ap < arg_e && *ap == '>') {
					direct = 1;
					ap++;
					while (ap < arg_e && isspace((unsigned char)*ap)) ap++;
				}
				css_compound_t *sub = calloc(1, sizeof(*sub));
				if (sub) {
					uint32_t sub_spec = 0;
					if (parse_compound(&ap, arg_e, sub, &sub_spec)) {
						if (out->has_sel) {
							free_compound(out->has_sel);
							free(out->has_sel);
						}
						out->has_sel = sub;
						out->has_direct_child = direct;
						*spec += sub_spec;
					} else {
						free_compound(sub);
						free(sub);
					}
				}
			} else {
				/* Unsupported pseudo-class (e.g. :nth-child) -> won't match */
				out->is_pseudo_el = 1;
			}
			free(pname);
			matched_any = 1;
			continue;
		}
		if (isalpha((unsigned char)c) || c == '-' || c == '_') {
			char *tag = NULL;
			p = parse_ident(p, end, &tag);
			if (!tag) return 0;
			free(out->tag);
			out->tag = tag;
			*spec += 1;
			matched_any = 1;
			continue;
		}
		/* Unknown character inside selector */
		return 0;
	}

	*pp = p;
	return matched_any;
}

static int parse_selector(const char *s, const char *end, css_rule_t *rule)
{
	trim_span(&s, &end);
	if (s >= end) return 0;
	rule->n_parts = 0;
	rule->spec = 0;

	char comb = 0;
	const char *p = s;
	while (p < end && rule->n_parts < CSS_MAX_PARTS) {
		while (p < end && isspace((unsigned char)*p)) p++;
		if (p >= end) break;
		if (rule->n_parts > 0 && (*p == '>' || *p == '+' || *p == '~')) {
			comb = *p++;
			while (p < end && isspace((unsigned char)*p)) p++;
		}
		css_compound_t comp;
		if (!parse_compound(&p, end, &comp, &rule->spec)) {
			for (uint8_t i = 0; i < rule->n_parts; i++)
				free_compound(&rule->parts[i]);
			rule->n_parts = 0;
			return 0;
		}
		rule->combs[rule->n_parts] = comb;
		rule->parts[rule->n_parts] = comp;
		rule->n_parts++;

		/* Determine next combinator */
		comb = ' ';
		const char *sp = p;
		while (p < end && isspace((unsigned char)*p)) p++;
		if (p < end && (*p == '>' || *p == '+' || *p == '~')) {
			comb = *p++;
		} else if (p == sp && p < end) {
			/* Trailing garbage */
			for (uint8_t i = 0; i < rule->n_parts; i++)
				free_compound(&rule->parts[i]);
			rule->n_parts = 0;
			return 0;
		}
	}
	return rule->n_parts > 0;
}

/* ── CSS Parser (top-level + @media) ────────────────────────────────────── */

static const char *skip_ws_comments(const char *p, const char *end)
{
	while (p < end) {
		if (isspace((unsigned char)*p)) {
			p++;
		} else if (p + 1 < end && p[0] == '/' && p[1] == '*') {
			p += 2;
			while (p + 1 < end && !(p[0] == '*' && p[1] == '/'))
				p++;
			if (p + 1 < end) p += 2;
		} else {
			break;
		}
	}
	return p;
}

static const char *find_matching_brace(const char *p, const char *end)
{
	int depth = 1;
	while (p < end && depth > 0) {
		if (*p == '/' && p + 1 < end && p[1] == '*') {
			p += 2;
			while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) p++;
			if (p + 1 < end) p += 2;
			continue;
		}
		if (*p == '"' || *p == '\'') {
			char q = *p++;
			while (p < end && *p != q) {
				if (*p == '\\' && p + 1 < end) p++;
				p++;
			}
			if (p < end) p++;
			continue;
		}
		if (*p == '{') depth++;
		else if (*p == '}') depth--;
		if (depth > 0) p++;
	}
	return p;
}

static css_media_t parse_media_query(const char *s, const char *end, css_media_t parent)
{
	css_media_t m = parent;
	char buf[512];
	size_t l = (size_t)(end - s);
	if (l >= sizeof(buf)) l = sizeof(buf) - 1;
	memcpy(buf, s, l);
	buf[l] = '\0';

	/* Lowercase copy for matching */
	for (size_t i = 0; i < l; i++)
		buf[i] = (char)tolower((unsigned char)buf[i]);

	if (strstr(buf, "prefers-color-scheme") && strstr(buf, "dark"))
		m.flags |= M_DARK;
	else if (strstr(buf, "prefers-color-scheme") && strstr(buf, "light"))
		m.flags |= M_LIGHT;

	if (strstr(buf, "prefers-reduced-motion") || strstr(buf, "pointer: coarse") ||
	    strstr(buf, "pointer:coarse")) {
		/* If comma-separated with max-width, still don't activate on desktop */
		m.flags |= M_NEVER;
	}

	if (strstr(buf, "orientation:portrait") || strstr(buf, "orientation: portrait"))
		m.flags |= M_PORTRAIT;

	const char *mw = strstr(buf, "max-width:");
	if (!mw) mw = strstr(buf, "max-width :");
	if (mw) {
		mw = strchr(mw, ':') + 1;
		int px = atoi(mw);
		if (px > 0) {
			m.flags |= M_MAX_W;
			m.max_w = (uint16_t)px;
		}
	}

	const char *minw = strstr(buf, "min-width:");
	if (!minw) minw = strstr(buf, "min-width :");
	if (minw) {
		minw = strchr(minw, ':') + 1;
		int px = atoi(minw);
		if (px > 0) {
			m.flags |= M_MIN_W;
			m.min_w = (uint16_t)px;
		}
	}

	return m;
}

static uint32_t parse_decl_block(css_sheet_t *sh, const char *s, const char *end,
                                 css_media_t media, int collect_vars)
{
	if (sh->n_blocks >= sh->cap_blocks) {
		uint32_t nc = sh->cap_blocks ? sh->cap_blocks * 2 : 128;
		sh->blocks = realloc(sh->blocks, nc * sizeof(css_block_t));
		sh->cap_blocks = nc;
	}
	uint32_t bidx = sh->n_blocks++;
	css_block_t *blk = &sh->blocks[bidx];
	blk->decls = NULL;
	blk->n_decls = 0;
	uint32_t cap = 0;

	const char *p = s;
	while (p < end) {
		p = skip_ws_comments(p, end);
		if (p >= end) break;

		const char *ps = p;
		while (p < end && *p != ':' && *p != ';') p++;
		if (p >= end || *p == ';') {
			if (p < end) p++;
			continue;
		}
		const char *pe = p;
		trim_span(&ps, &pe);
		p++; /* skip ':' */

		const char *vs = p;
		int par = 0;
		while (p < end) {
			if (*p == '/' && p + 1 < end && p[1] == '*') {
				p += 2;
				while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) p++;
				if (p + 1 < end) p += 2;
				continue;
			}
			if (*p == '"' || *p == '\'') {
				char q = *p++;
				while (p < end && *p != q) {
					if (*p == '\\' && p + 1 < end) p++;
					p++;
				}
				if (p < end) p++;
				continue;
			}
			if (*p == '(') par++;
			else if (*p == ')' && par > 0) par--;
			else if (*p == ';' && par == 0) break;
			p++;
		}
		const char *ve = p;
		if (p < end && *p == ';') p++;
		trim_span(&vs, &ve);
		if (ps >= pe || vs >= ve) continue;

		/* Strip !important */
		int imp = 0;
		if (ve - vs >= 10) {
			const char *q = ve - 9;
			if (ascii_strncasecmp(q, "important", 9) == 0) {
				const char *bang = q - 1;
				while (bang > vs && isspace((unsigned char)*bang)) bang--;
				if (bang >= vs && *bang == '!') {
					imp = 1;
					ve = bang;
					trim_span(&vs, &ve);
				}
			}
		}

		/* Custom property --foo: bar */
		if (pe - ps >= 2 && ps[0] == '-' && ps[1] == '-') {
			if (collect_vars) {
				if (sh->n_vars >= sh->cap_vars) {
					uint32_t nc = sh->cap_vars ? sh->cap_vars * 2 : 64;
					sh->vars = realloc(sh->vars, nc * sizeof(css_var_t));
					sh->cap_vars = nc;
				}
				sh->vars[sh->n_vars].name  = xstrndup(ps, (size_t)(pe - ps));
				sh->vars[sh->n_vars].val   = xstrndup(vs, (size_t)(ve - vs));
				sh->vars[sh->n_vars].media = media;
				sh->n_vars++;
			}
			continue;
		}

		if (blk->n_decls >= cap) {
			cap = cap ? cap * 2 : 8;
			blk->decls = realloc(blk->decls, cap * sizeof(css_decl_t));
		}
		blk->decls[blk->n_decls].prop = xstrndup(ps, (size_t)(pe - ps));
		blk->decls[blk->n_decls].val  = xstrndup(vs, (size_t)(ve - vs));
		blk->decls[blk->n_decls].important = (uint8_t)imp;
		blk->n_decls++;
	}
	return bidx;
}

static int parse_css_region(css_sheet_t *sh, const char *p, const char *end,
                            css_media_t media)
{
	int count = 0;
	while (p < end) {
		p = skip_ws_comments(p, end);
		if (p >= end) break;

		/* At-rule */
		if (*p == '@') {
			p++;
			const char *at_s = p;
			while (p < end && is_ident_char((unsigned char)*p)) p++;
			const char *at_e = p;
			const char *prelude_s = p;
			while (p < end && *p != '{' && *p != ';') p++;
			const char *prelude_e = p;
			if (p >= end) break;
			if (*p == ';') {
				p++;
				continue;
			}
			/* '{' */
			p++;
			const char *body_s = p;
			const char *body_e = find_matching_brace(p, end);
			p = (body_e < end) ? body_e + 1 : end;

			if ((at_e - at_s) == 5 && ascii_strncasecmp(at_s, "media", 5) == 0) {
				const char *cur = prelude_s;
				int par = 0;
				for (const char *q = prelude_s; q <= prelude_e; q++) {
					if (q < prelude_e) {
						if (*q == '(') par++;
						else if (*q == ')' && par > 0) par--;
					}
					if (q == prelude_e || (*q == ',' && par == 0)) {
						css_media_t sub_m = parse_media_query(cur, q, media);
						count += parse_css_region(sh, body_s, body_e, sub_m);
						cur = q + 1;
					}
				}
			} else if ((at_e - at_s) == 5 && ascii_strncasecmp(at_s, "layer", 5) == 0) {
				count += parse_css_region(sh, body_s, body_e, media);
			}
			/* @keyframes, @font-face, @supports etc. skipped cleanly */
			continue;
		}

		/* Qualified rule: selector_list { decls } */
		const char *sel_s = p;
		while (p < end && *p != '{') {
			if (*p == '/' && p + 1 < end && p[1] == '*') {
				p += 2;
				while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) p++;
				if (p + 1 < end) p += 2;
				continue;
			}
			p++;
		}
		if (p >= end) break;
		const char *sel_e = p;
		p++; /* skip '{' */
		const char *body_s = p;
		const char *body_e = find_matching_brace(p, end);
		p = (body_e < end) ? body_e + 1 : end;

		/* Check if selector mentions :root or html or body for var collection */
		int collect_vars = 0;
		for (const char *q = sel_s; q < sel_e; q++) {
			if (ascii_strncasecmp(q, ":root", 5) == 0 ||
			    ascii_strncasecmp(q, "html", 4) == 0 ||
			    ascii_strncasecmp(q, "body", 4) == 0) {
				collect_vars = 1;
				break;
			}
		}

		uint32_t bidx = parse_decl_block(sh, body_s, body_e, media, collect_vars);
		if (sh->blocks[bidx].n_decls == 0)
			continue;

		/* Split selector list by top-level commas */
		const char *cur = sel_s;
		int par = 0, brk = 0;
		for (const char *q = sel_s; q <= sel_e; q++) {
			if (q < sel_e) {
				if (*q == '(') par++;
				else if (*q == ')' && par > 0) par--;
				else if (*q == '[') brk++;
				else if (*q == ']' && brk > 0) brk--;
			}
			if (q == sel_e || (*q == ',' && par == 0 && brk == 0)) {
				css_rule_t r;
				memset(&r, 0, sizeof(r));
				if (parse_selector(cur, q, &r)) {
					r.block_idx = bidx;
					r.order = sh->next_order++;
					r.media = media;
					if (sh->n_rules >= sh->cap_rules) {
						uint32_t nc = sh->cap_rules ? sh->cap_rules * 2 : 256;
						sh->rules = realloc(sh->rules, nc * sizeof(css_rule_t));
						sh->cap_rules = nc;
					}
					sh->rules[sh->n_rules++] = r;
					count++;
				}
				cur = q + 1;
			}
		}
	}
	return count;
}

int lam_css_parse(uint32_t ss, const char *css, size_t len)
{
	if (!css || len == 0) return 0;
	css_sheet_t *sh = sheet_get_or_create(ss);
	if (!sh) return -1;
	css_media_t root_m = { M_NONE, 0, 0 };
	return parse_css_region(sh, css, css + len, root_m);
}

int lam_css_load_file(uint32_t ss, const char *path)
{
	if (!path) return -1;
	FILE *fp = fopen(path, "rb");
	if (!fp) return -1;
	if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
	long sz = ftell(fp);
	if (sz <= 0) { fclose(fp); return 0; }
	rewind(fp);
	char *buf = malloc((size_t)sz + 1);
	if (!buf) { fclose(fp); return -1; }
	size_t nr = fread(buf, 1, (size_t)sz, fp);
	fclose(fp);
	buf[nr] = '\0';
	int res = lam_css_parse(ss, buf, nr);
	free(buf);
	return res;
}

int lam_css_set_var(uint32_t ss, const char *name, const char *val)
{
	if (!name || !*name) return -1;
	css_sheet_t *sh = sheet_get_or_create(ss);
	if (!sh) return -1;
	for (int i = (int)sh->n_vars - 1; i >= 0; i--) {
		if (sh->vars[i].media.flags == M_NONE && strcmp(sh->vars[i].name, name) == 0) {
			char *nv = xstrndup(val ? val : "", val ? strlen(val) : 0);
			if (!nv) return -1;
			free(sh->vars[i].val);
			sh->vars[i].val = nv;
			return 0;
		}
	}
	if (sh->n_vars >= sh->cap_vars) {
		uint32_t nc = sh->cap_vars ? sh->cap_vars * 2 : 64;
		css_var_t *nv = realloc(sh->vars, nc * sizeof(css_var_t));
		if (!nv) return -1;
		sh->vars = nv;
		sh->cap_vars = nc;
	}
	sh->vars[sh->n_vars].name = xstrndup(name, strlen(name));
	sh->vars[sh->n_vars].val  = xstrndup(val ? val : "", val ? strlen(val) : 0);
	sh->vars[sh->n_vars].media.flags = M_NONE;
	sh->vars[sh->n_vars].media.max_w = 0;
	sh->vars[sh->n_vars].media.min_w = 0;
	sh->n_vars++;
	return 0;
}

/* ── Selector matcher ───────────────────────────────────────────────────── */

int lam_ui_has_class(const lam_ui_div_t *div, const char *cls, size_t len)
{
	if (!div || !div->class_name || !cls || len == 0)
		return 0;
	const char *p = div->class_name;
	while (*p) {
		while (*p && isspace((unsigned char)*p)) p++;
		if (!*p) break;
		const char *s = p;
		while (*p && !isspace((unsigned char)*p)) p++;
		if ((size_t)(p - s) == len && memcmp(s, cls, len) == 0)
			return 1;
	}
	return 0;
}

static int match_compound(const lam_ui_div_t *d, const css_compound_t *c);

static int subtree_has_compound(const lam_ui_div_t *d, const css_compound_t *sel,
                                int direct_only)
{
	for (const lam_ui_div_t *c = d->first_child; c; c = c->next_sibling) {
		if (match_compound(c, sel))
			return 1;
		if (!direct_only && subtree_has_compound(c, sel, 0))
			return 1;
	}
	return 0;
}

static int match_compound(const lam_ui_div_t *d, const css_compound_t *c)
{
	if (c->is_pseudo_el)
		return 0;
	if (c->is_root && d->parent != NULL)
		return 0;
	if (c->tag && (!d->tag || ascii_strcasecmp(d->tag, c->tag) != 0))
		return 0;
	if (c->id && (!d->id || strcmp(d->id, c->id) != 0))
		return 0;
	for (uint8_t i = 0; i < c->n_classes; i++) {
		if (!lam_ui_has_class(d, c->classes[i], strlen(c->classes[i])))
			return 0;
	}
	if (c->req_pseudo && (d->pseudo_mask & c->req_pseudo) != c->req_pseudo)
		return 0;
	if (c->is_first_child && d->parent && d->parent->first_child != d)
		return 0;
	if (c->is_last_child && d->next_sibling != NULL)
		return 0;
	if (c->is_empty && (d->first_child != NULL || (d->text && d->text[0] != '\0')))
		return 0;

	for (uint8_t i = 0; i < c->n_attrs; i++) {
		const css_attr_t *a = &c->attrs[i];
		const char *v = NULL;
		if (ascii_strcasecmp(a->name, "id") == 0) v = d->id;
		else if (ascii_strcasecmp(a->name, "class") == 0) v = d->class_name;
		else if (ascii_strcasecmp(a->name, "style") == 0) v = d->inline_style;
		else v = lam_ui_get_attr(d, a->name);

		if (!v) return 0;
		if (a->op == '=' && (!a->val || strcmp(v, a->val) != 0)) return 0;
		if (a->op == '*' && (!a->val || !strstr(v, a->val))) return 0;
		if (a->op == '^' && (!a->val || strncmp(v, a->val, strlen(a->val)) != 0)) return 0;
		if (a->op == '$') {
			if (!a->val) return 0;
			size_t vl = strlen(v), al = strlen(a->val);
			if (vl < al || strcmp(v + vl - al, a->val) != 0) return 0;
		}
	}

	for (uint8_t i = 0; i < c->n_nots; i++) {
		if (match_compound(d, &c->nots[i]))
			return 0;
	}

	if (c->has_sel) {
		if (!subtree_has_compound(d, c->has_sel, c->has_direct_child))
			return 0;
	}

	return 1;
}

static const lam_ui_div_t *prev_sibling(const lam_ui_div_t *d)
{
	if (!d || !d->parent) return NULL;
	const lam_ui_div_t *prev = NULL;
	for (const lam_ui_div_t *c = d->parent->first_child; c && c != d; c = c->next_sibling)
		prev = c;
	return prev;
}

static int match_selector_part(const lam_ui_div_t *d, const css_rule_t *r, int idx)
{
	if (!d || !match_compound(d, &r->parts[idx]))
		return 0;
	if (idx == 0)
		return 1;

	char comb = r->combs[idx];
	if (comb == '>') {
		return match_selector_part(d->parent, r, idx - 1);
	}
	if (comb == ' ') {
		for (const lam_ui_div_t *p = d->parent; p; p = p->parent) {
			if (match_selector_part(p, r, idx - 1))
				return 1;
		}
		return 0;
	}
	if (comb == '+') {
		return match_selector_part(prev_sibling(d), r, idx - 1);
	}
	if (comb == '~') {
		for (const lam_ui_div_t *p = prev_sibling(d); p; p = prev_sibling(p)) {
			if (match_selector_part(p, r, idx - 1))
				return 1;
		}
		return 0;
	}
	return 0;
}

void lam_css_apply_node(lam_ui_div_t *div, uint32_t ss, lam_ui_style_t *dst)
{
	if (div && div->tag && ascii_strcasecmp(div->tag, "button") == 0)
		dst->text_align = LAM_UI_TEXT_ALIGN_CENTER;
	if (div && lam_ui_get_attr(div, "hidden") != NULL)
		dst->display = LAM_UI_DISPLAY_NONE;
	css_sheet_t *sh = sheet_find(ss);
	if (!sh || sh->n_rules == 0)
		return;

	const css_rule_t *hits_stack[128];
	const css_rule_t **hits = hits_stack;
	uint32_t n_hits = 0, cap = 128;

	for (uint32_t i = 0; i < sh->n_rules; i++) {
		const css_rule_t *r = &sh->rules[i];
		if (!media_matches(r->media))
			continue;
		const css_compound_t *last = &r->parts[r->n_parts - 1];
		/* Fast reject on rightmost compound */
		if (last->is_pseudo_el) continue;
		if (last->id && (!div->id || strcmp(div->id, last->id) != 0)) continue;
		if (last->tag && (!div->tag || ascii_strcasecmp(div->tag, last->tag) != 0)) continue;
		if (last->n_classes > 0 && !div->class_name) continue;

		if (!match_selector_part(div, r, (int)r->n_parts - 1))
			continue;

		if (n_hits >= cap) {
			uint32_t nc = cap * 2;
			if (hits == hits_stack) {
				hits = malloc(nc * sizeof(*hits));
				memcpy(hits, hits_stack, n_hits * sizeof(*hits));
			} else {
				hits = realloc(hits, nc * sizeof(*hits));
			}
			cap = nc;
		}
		hits[n_hits++] = r;
	}

	/* Insertion sort by (spec, order) */
	for (uint32_t i = 1; i < n_hits; i++) {
		const css_rule_t *key = hits[i];
		uint32_t j = i;
		while (j > 0 && (hits[j - 1]->spec > key->spec ||
		       (hits[j - 1]->spec == key->spec && hits[j - 1]->order > key->order))) {
			hits[j] = hits[j - 1];
			j--;
		}
		hits[j] = key;
	}

	/* Apply declarations in sorted cascade order */
	for (uint32_t i = 0; i < n_hits; i++) {
		const css_block_t *blk = &sh->blocks[hits[i]->block_idx];
		for (uint32_t j = 0; j < blk->n_decls; j++) {
			const css_decl_t *d = &blk->decls[j];
			apply_decl(dst, sh, d->prop, d->val, d->important);
		}
	}

	if (hits != hits_stack)
		free(hits);
}

void lam_css_apply_inline_str(lam_ui_style_t *dst, uint32_t ss, const char *decls)
{
	if (!decls || !*decls) return;
	const css_sheet_t *sh = sheet_find(ss);

	const char *p = decls;
	const char *end = decls + strlen(decls);

	while (p < end) {
		p = skip_ws_comments(p, end);
		if (p >= end) break;
		const char *ps = p;
		while (p < end && *p != ':' && *p != ';') p++;
		if (p >= end || *p == ';') { if (p < end) p++; continue; }
		const char *pe = p;
		trim_span(&ps, &pe);
		p++;

		const char *vs = p;
		int par = 0;
		while (p < end) {
			if (*p == '(') par++;
			else if (*p == ')' && par > 0) par--;
			else if (*p == ';' && par == 0) break;
			p++;
		}
		const char *ve = p;
		if (p < end && *p == ';') p++;
		trim_span(&vs, &ve);
		if (ps >= pe || vs >= ve) continue;

		int imp = 0;
		if (ve - vs >= 10) {
			const char *q = ve - 9;
			if (ascii_strncasecmp(q, "important", 9) == 0) {
				const char *bang = q - 1;
				while (bang > vs && isspace((unsigned char)*bang)) bang--;
				if (bang >= vs && *bang == '!') {
					imp = 1;
					ve = bang;
					trim_span(&vs, &ve);
				}
			}
		}

		char prop[128], val[512];
		size_t pl = (size_t)(pe - ps);
		size_t vl = (size_t)(ve - vs);
		if (pl >= sizeof(prop)) pl = sizeof(prop) - 1;
		if (vl >= sizeof(val)) vl = sizeof(val) - 1;
		memcpy(prop, ps, pl); prop[pl] = '\0';
		memcpy(val, vs, vl);  val[vl] = '\0';

		apply_decl(dst, sh, prop, val, imp);
	}
}
