/**
 * test_textures.c - Texture API tests for liblamina
 * Tests texture loading, drawing, tinting, and manipulation
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ttypt/lam.h>
#include <ttypt/corm.h>

static void test_tex_load(void) {
	uint32_t tex_ref;
	
	/* Load test texture */
	tex_ref = lam_tex_load("tests/fixtures/test_texture.png");
	assert(tex_ref != CM_MISS);
	
	/* Load same texture again (should return same ref) */
	uint32_t tex_ref2 = lam_tex_load("tests/fixtures/test_texture.png");
	assert(tex_ref2 == tex_ref);
	
	/* Load different texture */
	uint32_t tex_small = lam_tex_load("tests/fixtures/test_small.png");
	assert(tex_small != CM_MISS);
	assert(tex_small != tex_ref);
	
	printf("  test_tex_load: PASS\n");
}

static void test_tex_size(void) {
	uint32_t tex_ref, w = 0, h = 0;
	
	tex_ref = lam_tex_load("tests/fixtures/test_texture.png");
	assert(tex_ref != CM_MISS);
	
	lam_tex_size(&w, &h, tex_ref);
	assert(w == 64);
	assert(h == 64);
	
	/* Test small texture */
	tex_ref = lam_tex_load("tests/fixtures/test_small.png");
	lam_tex_size(&w, &h, tex_ref);
	assert(w == 16);
	assert(h == 16);
	
	printf("  test_tex_size: PASS (64x64, 16x16)\n");
}

static void test_tex_draw(void) {
	uint32_t screen_w, screen_h;
	uint32_t tex_ref;
	
	lam_size(&screen_w, &screen_h);
	tex_ref = lam_tex_load("tests/fixtures/test_texture.png");
	assert(tex_ref != CM_MISS);
	
	/* Clear screen */
	lam_fill(0, 0, screen_w, screen_h, 0xFF000000);
	
	/* Draw texture at different positions */
	lam_tex_draw(tex_ref, 0, 0, 64, 64);  /* Original size */
	lam_tex_draw(tex_ref, 100, 0, 128, 128);  /* Scaled up */
	lam_tex_draw(tex_ref, 250, 0, 32, 32);  /* Scaled down */
	
	/* Edge cases */
	lam_tex_draw(tex_ref, 0, 0, 0, 0);  /* Zero size - should not crash */
	lam_tex_draw(tex_ref, -10, -10, 20, 20);  /* Partially off-screen */
	
	lam_flush();
	
	printf("  test_tex_draw: PASS\n");
}

static void test_tex_draw_x(void) {
	uint32_t screen_w, screen_h;
	uint32_t tex_ref;
	
	lam_size(&screen_w, &screen_h);
	tex_ref = lam_tex_load("tests/fixtures/test_texture.png");
	assert(tex_ref != CM_MISS);
	
	lam_fill(0, 0, screen_w, screen_h, 0xFF000000);
	
	/* Draw full texture */
	lam_tex_draw_x(tex_ref, 0, 0, 0, 0, 64, 64, 64, 64, lam_default_tint);
	
	/* Draw partial texture (top-left quadrant only) */
	lam_tex_draw_x(tex_ref, 100, 0, 0, 0, 32, 32, 32, 32, lam_default_tint);
	
	/* Draw bottom-right quadrant */
	lam_tex_draw_x(tex_ref, 150, 0, 32, 32, 32, 32, 64, 64, lam_default_tint);
	
	/* Draw with different tints */
	lam_tex_draw_x(tex_ref, 0, 100, 0, 0, 64, 64, 64, 64, 0xFFFF0000);  /* Red tint */
	lam_tex_draw_x(tex_ref, 100, 100, 0, 0, 64, 64, 64, 64, 0xFF00FF00);  /* Green tint */
	lam_tex_draw_x(tex_ref, 200, 100, 0, 0, 64, 64, 64, 64, 0xFF0000FF);  /* Blue tint */
	lam_tex_draw_x(tex_ref, 300, 100, 0, 0, 64, 64, 64, 64, 0x80FFFFFF);  /* Semi-transparent */
	
	lam_flush();
	
	printf("  test_tex_draw_x: PASS\n");
}

static void test_tex_tint(void) {
	uint32_t screen_w, screen_h;
	uint32_t tex_ref;
	
	lam_size(&screen_w, &screen_h);
	tex_ref = lam_tex_load("tests/fixtures/test_texture.png");
	
	lam_fill(0, 0, screen_w, screen_h, 0xFF000000);
	
	/* Draw with global tint */
	lam_tint(0xFFFF0000);  /* Red */
	lam_tex_draw(tex_ref, 0, 0, 64, 64);
	
	lam_tint(0xFF00FF00);  /* Green */
	lam_tex_draw(tex_ref, 70, 0, 64, 64);
	
	lam_tint(0xFF0000FF);  /* Blue */
	lam_tex_draw(tex_ref, 140, 0, 64, 64);
	
	/* Reset tint */
	lam_tint(lam_default_tint);
	lam_tex_draw(tex_ref, 210, 0, 64, 64);
	
	lam_flush();
	
	printf("  test_tex_tint: PASS\n");
}

static void test_tex_pick_paint(void) {
	uint32_t tex_ref;
	uint32_t color;
	
	tex_ref = lam_tex_load("tests/fixtures/test_texture.png");
	assert(tex_ref != CM_MISS);
	
	/* Pick a color from the texture (top-left should be red) */
	color = lam_tex_pick(tex_ref, 5, 5);
	/* Note: Color should be 0xFFFF0000 (ARGB) or 0xFF0000FF (ABGR) depending on format */
	assert((color & 0xFF000000) != 0);  /* Has alpha */
	
	/* Paint a pixel */
	lam_tex_paint(tex_ref, 10, 10, 0xFFFFFFFF);  /* White */
	
	/* Verify the pixel was changed */
	color = lam_tex_pick(tex_ref, 10, 10);
	assert(color == 0xFFFFFFFF);
	
	printf("  test_tex_pick_paint: PASS\n");
}

static void test_multiple_textures(void) {
	uint32_t screen_w, screen_h;
	uint32_t tex1, tex2;
	
	lam_size(&screen_w, &screen_h);
	
	tex1 = lam_tex_load("tests/fixtures/test_texture.png");
	tex2 = lam_tex_load("tests/fixtures/test_small.png");
	
	assert(tex1 != CM_MISS);
	assert(tex2 != CM_MISS);
	assert(tex1 != tex2);
	
	lam_fill(0, 0, screen_w, screen_h, 0xFF000000);
	
	/* Draw both textures */
	lam_tex_draw(tex1, 0, 0, 64, 64);
	lam_tex_draw(tex2, 70, 0, 64, 64);  /* Scale up small texture */
	lam_tex_draw(tex1, 140, 0, 32, 32);  /* Scale down large texture */
	lam_tex_draw(tex2, 180, 0, 16, 16);  /* Original size */
	
	lam_flush();
	
	printf("  test_multiple_textures: PASS\n");
}

int main(void) {
	printf("test_textures:\n");
	lam_init();
	
	test_tex_load();
	test_tex_size();
	test_tex_draw();
	test_tex_draw_x();
	test_tex_tint();
	test_tex_pick_paint();
	test_multiple_textures();
	
	printf("test_textures: ALL TESTS PASSED\n");
	return 0;
}
