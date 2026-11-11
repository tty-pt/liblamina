/**
 * test_tilemaps.c - Tilemap API tests for liblamina
 * Tests tilemap creation and tile rendering
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ttypt/lam.h>
#include <ttypt/lam-tm.h>
#include <ttypt/corm.h>

static void test_tm_new(void) {
	uint32_t tex_ref, tm_ref;
	
	/* Load texture first */
	tex_ref = lam_tex_load("tests/fixtures/test_tilemap.png");
	assert(tex_ref != CM_MISS);
	
	/* Create tilemap with 16x16 tiles */
	tm_ref = lam_tm_new(tex_ref, 16, 16);
	assert(tm_ref != CM_MISS);
	
	printf("  test_tm_new: PASS (ref=%u)\n", tm_ref);
}

static void test_tm_get(void) {
	uint32_t tex_ref, tm_ref;
	const lam_tm_t *tm;
	
	tex_ref = lam_tex_load("tests/fixtures/test_tilemap.png");
	tm_ref = lam_tm_new(tex_ref, 16, 16);
	
	tm = lam_tm_get(tm_ref);
	assert(tm != NULL);
	assert(tm->img == tex_ref);
	assert(tm->w == 16);
	assert(tm->h == 16);
	assert(tm->nx == 8);  /* 128/16 = 8 tiles wide */
	assert(tm->ny == 8);  /* 128/16 = 8 tiles tall */
	
	printf("  test_tm_get: PASS (nx=%u, ny=%u)\n", tm->nx, tm->ny);
}

static void test_tile_draw(void) {
	uint32_t screen_w, screen_h;
	uint32_t tex_ref, tm_ref;
	
	lam_size(&screen_w, &screen_h);
	
	tex_ref = lam_tex_load("tests/fixtures/test_tilemap.png");
	tm_ref = lam_tm_new(tex_ref, 16, 16);
	
	lam_fill(0, 0, screen_w, screen_h, 0xFF000000);
	
	/* Draw individual tiles */
	lam_tile_draw(tm_ref, 0, 0, 0, 16, 16, 1, 1);  /* Tile 0 at (0,0) */
	lam_tile_draw(tm_ref, 1, 20, 0, 16, 16, 1, 1);  /* Tile 1 at (20,0) */
	lam_tile_draw(tm_ref, 8, 40, 0, 16, 16, 1, 1);  /* Tile 8 at (40,0) - second row */
	
	/* Draw scaled tiles */
	lam_tile_draw(tm_ref, 0, 0, 20, 32, 32, 1, 1);  /* 2x scale */
	lam_tile_draw(tm_ref, 1, 40, 20, 8, 8, 1, 1);   /* 0.5x scale */
	
	lam_flush();
	
	printf("  test_tile_draw: PASS\n");
}

static void test_tile_repeat(void) {
	uint32_t screen_w, screen_h;
	uint32_t tex_ref, tm_ref;
	
	lam_size(&screen_w, &screen_h);
	
	tex_ref = lam_tex_load("tests/fixtures/test_tilemap.png");
	tm_ref = lam_tm_new(tex_ref, 16, 16);
	
	lam_fill(0, 0, screen_w, screen_h, 0xFF000000);
	
	/* Draw repeated tiles */
	lam_tile_draw(tm_ref, 0, 0, 0, 16, 16, 3, 2);  /* 3x2 grid */
	lam_tile_draw(tm_ref, 5, 0, 50, 16, 16, 4, 3);  /* 4x3 grid */
	
	lam_flush();
	
	printf("  test_tile_draw_repeat: PASS\n");
}

static void test_tile_grid(void) {
	uint32_t screen_w, screen_h;
	uint32_t tex_ref, tm_ref;
	const lam_tm_t *tm;
	
	lam_size(&screen_w, &screen_h);
	
	tex_ref = lam_tex_load("tests/fixtures/test_tilemap.png");
	tm_ref = lam_tm_new(tex_ref, 16, 16);
	tm = lam_tm_get(tm_ref);
	
	lam_fill(0, 0, screen_w, screen_h, 0xFF000000);
	
	/* Draw a grid of different tiles */
	for (uint32_t ty = 0; ty < 4 && ty < tm->ny; ty++) {
		for (uint32_t tx = 0; tx < 4 && tx < tm->nx; tx++) {
			uint32_t tile_idx = ty * tm->nx + tx;
			lam_tile_draw(tm_ref, tile_idx, tx * 20, ty * 20, 16, 16, 1, 1);
		}
	}
	
	lam_flush();
	
	printf("  test_tile_grid: PASS\n");
}

static void test_multiple_tilemaps(void) {
	uint32_t tex1, tex2, tm1, tm2;
	
	/* Create two different tilemaps */
	tex1 = lam_tex_load("tests/fixtures/test_tilemap.png");
	tex2 = lam_tex_load("tests/fixtures/test_texture.png");
	
	tm1 = lam_tm_new(tex1, 16, 16);
	tm2 = lam_tm_new(tex2, 32, 32);  /* Different tile size */
	
	assert(tm1 != CM_MISS);
	assert(tm2 != CM_MISS);
	assert(tm1 != tm2);
	
	const lam_tm_t *tilemap1 = lam_tm_get(tm1);
	const lam_tm_t *tilemap2 = lam_tm_get(tm2);
	
	assert(tilemap1->w == 16);
	assert(tilemap2->w == 32);
	
	printf("  test_multiple_tilemaps: PASS\n");
}

static void test_tile_edge_cases(void) {
	uint32_t screen_w, screen_h;
	uint32_t tex_ref, tm_ref;
	const lam_tm_t *tm;
	
	lam_size(&screen_w, &screen_h);
	
	tex_ref = lam_tex_load("tests/fixtures/test_tilemap.png");
	tm_ref = lam_tm_new(tex_ref, 16, 16);
	tm = lam_tm_get(tm_ref);
	
	lam_fill(0, 0, screen_w, screen_h, 0xFF000000);
	
	/* Last valid tile */
	uint32_t last_tile = tm->nx * tm->ny - 1;
	lam_tile_draw(tm_ref, last_tile, 0, 0, 16, 16, 1, 1);
	
	/* Zero-size tiles (should not crash) */
	lam_tile_draw(tm_ref, 0, 0, 0, 0, 0, 1, 1);
	
	/* Zero repeats (should not crash) */
	lam_tile_draw(tm_ref, 0, 0, 0, 16, 16, 0, 0);
	
	lam_flush();
	
	printf("  test_tile_edge_cases: PASS\n");
}

static void test_small_tiles(void) {
	uint32_t tex_ref, tm_ref;
	const lam_tm_t *tm;
	
	/* Use small texture for 8x8 tiles */
	tex_ref = lam_tex_load("tests/fixtures/test_small.png");
	tm_ref = lam_tm_new(tex_ref, 8, 8);
	tm = lam_tm_get(tm_ref);
	
	assert(tm->w == 8);
	assert(tm->h == 8);
	assert(tm->nx == 2);  /* 16/8 = 2 */
	assert(tm->ny == 2);  /* 16/8 = 2 */
	
	printf("  test_small_tiles: PASS\n");
}

static void test_tile_map_draw(void) {
	uint32_t tex_ref, tm_ref;
	tex_ref = lam_tex_load("tests/fixtures/test_tilemap.png");
	tm_ref = lam_tm_new(tex_ref, 16, 16);

	uint16_t map[6] = { 0, 1, 2, 3, 4, 5 };
	lam_tile_map_draw(tm_ref, map, 3, 2, 10, 10, 2);
	lam_flush();

	printf("  test_tile_map_draw: PASS\n");
}

int main(void) {
	printf("test_tilemaps:\n");
	lam_init();
	
	test_tm_new();
	test_tm_get();
	test_tile_draw();
	test_tile_repeat();
	test_tile_grid();
	test_tile_map_draw();
	test_multiple_tilemaps();
	test_tile_edge_cases();
	test_small_tiles();
	
	printf("test_tilemaps: ALL TESTS PASSED\n");
	return 0;
}
