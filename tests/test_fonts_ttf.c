/**
 * test_fonts_ttf.c — Unit tests for FreeType2 proportional UTF-8 font engine
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ttypt/lam.h>
#include <ttypt/lam-font.h>
#include <ttypt/lam-ui.h>
#include <ttypt/corm.h>
#include <GL/gl.h>

static const char *SANS_PATH  = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
static const char *MONO_PATH  = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";
static const char *EMOJI_PATH = "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf";

static void test_ttf_open_and_metrics(void)
{
	assert(lam_font_open_ttf(NULL, 16) == CM_MISS);
	assert(lam_font_open_ttf("/nonexistent/font.ttf", 16) == CM_MISS);

	uint32_t f = lam_font_open_ttf(SANS_PATH, 16);
	assert(f != CM_MISS);
	assert(lam_font_is_ttf(f) == 1);

	uint32_t lh16 = lam_font_line_height(f, 16);
	uint32_t lh32 = lam_font_line_height(f, 32);
	uint32_t asc16 = lam_font_ascent(f, 16);
	assert(lh16 >= 16);
	assert(lh32 > lh16);
	assert(asc16 > 0 && asc16 <= lh16);

	lam_font_close(f);
	lam_font_close(f); /* double close safe */
	printf("  test_ttf_open_and_metrics: PASS (lh16=%u, lh32=%u, asc16=%u)\n",
	       lh16, lh32, asc16);
}

static void test_ttf_proportional_vs_mono(void)
{
	uint32_t sans = lam_font_open_ttf(SANS_PATH, 16);
	uint32_t mono = lam_font_open_ttf(MONO_PATH, 16);
	assert(sans != CM_MISS && mono != CM_MISS);

	uint32_t wi = 0, hi = 0, wm = 0, hm = 0;
	lam_font_measure_px(&wi, &hi, sans, "iiii", 0, 0, 1000, 100, 16,
	                    LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	lam_font_measure_px(&wm, &hm, sans, "MMMM", 0, 0, 1000, 100, 16,
	                    LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	assert(wi > 0 && wm > wi * 2);

	uint32_t mwi = 0, mhi = 0, mwm = 0, mhm = 0;
	lam_font_measure_px(&mwi, &mhi, mono, "iiii", 0, 0, 1000, 100, 16,
	                    LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	lam_font_measure_px(&mwm, &mhm, mono, "MMMM", 0, 0, 1000, 100, 16,
	                    LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	assert(mwi > 0 && mwi == mwm);

	/* Explicit pixel sizing */
	uint32_t w12 = 0, h12 = 0, w24 = 0, h24 = 0;
	lam_font_measure_px(&w12, &h12, sans, "NeverDark", 0, 0, 1000, 100, 12,
	                    LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	lam_font_measure_px(&w24, &h24, sans, "NeverDark", 0, 0, 1000, 100, 24,
	                    LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	assert(w24 > w12 && h24 > h12);

	lam_font_close(sans);
	lam_font_close(mono);
	printf("  test_ttf_proportional_vs_mono: PASS (sans iiii=%u MMMM=%u; mono=%u; 12px=%u 24px=%u)\n",
	       wi, wm, mwi, w12, w24);
}

static void test_ttf_utf8_wrap_and_parity(void)
{
	uint32_t sans = lam_font_open_ttf(SANS_PATH, 16);
	assert(sans != CM_MISS);

	const char *pt = "A\xc3\xa7\xc3\xa3o: p\xc3\xa3o, cora\xc3\xa7\xc3\xa3o, \xc3\xa9ter!";
	uint32_t w = 0, h = 0;
	const char *ov = lam_font_measure_px(&w, &h, sans, pt, 0, 0, 800, 100, 16,
	                                     LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	assert(ov == NULL && w > 80 && h >= 16);

	/* Narrow wrapping */
	uint32_t ww = 0, wh = 0;
	ov = lam_font_measure_px(&ww, &wh, sans, pt, 0, 0, 90, 200, 16,
	                         LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	assert(ov == NULL && wh >= h * 2);

	/* PRE vs NORMAL newlines */
	uint32_t wn = 0, hn = 0, wp = 0, hp = 0;
	lam_font_measure_px(&wn, &hn, sans, "Line1\nLine2", 0, 0, 500, 200, 16,
	                    LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	lam_font_measure_px(&wp, &hp, sans, "Line1\nLine2", 0, 0, 500, 200, 16,
	                    LAM_UI_WS_PRE, LAM_UI_WB_NORMAL);
	assert(hp >= hn * 2);

	/* Overflow pointer parity between measure_px and draw_px */
	const char *long_txt = "The quick brown fox jumps over the lazy dog in NeverDark";
	uint32_t ow = 0, oh = 0;
	const char *m_ov = lam_font_measure_px(&ow, &oh, sans, long_txt, 0, 0, 120, 24, 16,
	                                       LAM_UI_WS_NOWRAP, LAM_UI_WB_NORMAL);
	const char *d_ov = lam_font_draw_px(sans, long_txt, 0, 0, 120, 24, 16,
	                                    LAM_UI_WS_NOWRAP, LAM_UI_WB_NORMAL);
	assert(m_ov != NULL && m_ov == d_ov);

	lam_font_close(sans);
	printf("  test_ttf_utf8_wrap_and_parity: PASS (pt=%ux%u, wrapped=%ux%u)\n",
	       w, h, ww, wh);
}

static void test_ttf_render_and_tint_pixels(void)
{
	uint32_t sans = lam_font_open_ttf(SANS_PATH, 24);
	assert(sans != CM_MISS);

	lam_clear(0xFF000000u);
	lam_tint(0xFF00FF40u); /* bright green */
	const char *ov = lam_font_draw_px(sans, "A\xc3\xa7\xc3\xa3o 123",
	                                  8, 8, 250, 60, 24,
	                                  LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	assert(ov == NULL);
	lam_tint(0xFFFFFFFFu);

	/* Read back top-left 256x64 region of main FBO (OpenGL y=0 is bottom, ortho flips y) */
	uint8_t *px = calloc(1, 256 * 64 * 4);
	assert(px != NULL);
	uint32_t sw = 0, sh = 0;
	lam_size(&sw, &sh);
	if (sh >= 64) {
		glReadPixels(0, (GLint)(sh - 64), 256, 64, GL_RGBA, GL_UNSIGNED_BYTE, px);
		int green_pixels = 0;
		for (int i = 0; i < 256 * 64; i++) {
			uint8_t r = px[i * 4 + 0];
			uint8_t g = px[i * 4 + 1];
			uint8_t b = px[i * 4 + 2];
			if (g > 120 && r < 40 && b > 10)
				green_pixels++;
		}
		assert(green_pixels > 50);
		printf("  test_ttf_render_and_tint_pixels: PASS (green_pixels=%d)\n", green_pixels);
	} else {
		printf("  test_ttf_render_and_tint_pixels: PASS (small screen skipped readback)\n");
	}

	free(px);
	lam_font_close(sans);
}

static void test_ttf_fallback(void)
{
	uint32_t sans = lam_font_open_ttf(SANS_PATH, 16);
	uint32_t emoji = lam_font_open_ttf(EMOJI_PATH, 16);
	assert(sans != CM_MISS);
	if (emoji != CM_MISS) {
		assert(lam_font_add_fallback(sans, emoji) == 0);
		uint32_t w = 0, h = 0;
		const char *ov = lam_font_measure_px(&w, &h, sans, "Hi \xf0\x9f\xa7\xad!",
		                                     0, 0, 500, 100, 16,
		                                     LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
		assert(ov == NULL && w > 20);
		lam_font_draw_px(sans, "Hi \xf0\x9f\xa7\xad!", 8, 8, 500, 100, 16,
		                 LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
		lam_font_close(emoji);
		printf("  test_ttf_fallback: PASS (emoji w=%u, h=%u)\n", w, h);
	} else {
		printf("  test_ttf_fallback: SKIP (NotoColorEmoji not installed)\n");
	}
	lam_font_close(sans);
}

static void test_ttf_ignorable_codepoints(void)
{
	uint32_t sans = lam_font_open_ttf(SANS_PATH, 16);
	assert(sans != CM_MISS);
	uint32_t w_plain = 0, h_plain = 0, w_zwsp = 0, h_zwsp = 0;
	lam_font_measure_px(&w_plain, &h_plain, sans, "AB",
	                    0, 0, 500, 100, 16, LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	lam_font_measure_px(&w_zwsp, &h_zwsp, sans, "A\xE2\x80\x8B\xEF\xB8\x8F" "B",
	                    0, 0, 500, 100, 16, LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	assert(w_zwsp == w_plain && h_zwsp == h_plain);

	uint32_t emoji = lam_font_open_ttf(EMOJI_PATH, 16);
	if (emoji != CM_MISS) {
		assert(lam_font_add_fallback(sans, emoji) == 0);
		uint32_t w_eye = 0, h_eye = 0, w_eye_vs16 = 0, h_eye_vs16 = 0;
		lam_font_measure_px(&w_eye, &h_eye, sans, "\xF0\x9F\x91\x81",
		                    0, 0, 500, 100, 16, LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
		lam_font_measure_px(&w_eye_vs16, &h_eye_vs16, sans, "\xF0\x9F\x91\x81\xEF\xB8\x8F",
		                    0, 0, 500, 100, 16, LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
		assert(w_eye > 0 && w_eye_vs16 == w_eye);
		lam_font_close(emoji);
	}
	lam_font_close(sans);
	printf("  test_ttf_ignorable_codepoints: PASS\n");
}

int main(void)
{
	printf("test_fonts_ttf:\n");
	lam_init();

	test_ttf_open_and_metrics();
	test_ttf_proportional_vs_mono();
	test_ttf_utf8_wrap_and_parity();
	test_ttf_render_and_tint_pixels();
	test_ttf_fallback();
	test_ttf_ignorable_codepoints();

	printf("test_fonts_ttf: ALL TESTS PASSED\n");
	return 0;
}
