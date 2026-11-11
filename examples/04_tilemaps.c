/**
 * 04_tilemaps.c - Tilemap rendering example
 * 
 * Demonstrates:
 * - Creating tilemaps from texture atlases
 * - Drawing individual tiles by index
 * - Building a simple tile-based scene
 * - Querying tilemap properties
 * 
 * To compile:
 *   cc -o 04_tilemaps 04_tilemaps.c -llamina
 * 
 * Note: Requires a tilemap PNG (e.g., ../tests/fixtures/test_tilemap.png)
 */

#include <ttypt/lam.h>
#include <ttypt/lam-tm.h>
#include <stdio.h>

int main(void)
{
	uint32_t w, h;
	uint32_t tex_ref, tm_ref;
	const lam_tm_t *tm;
	
	/* Get screen dimensions */
	lam_size(&w, &h);
	
	/* Fill background */
	lam_fill(0, 0, w, h, 0xFF0F0F0F);
	
	/* Load tilemap texture */
	tex_ref = lam_tex_load("../tests/fixtures/test_tilemap.png");
	if (tex_ref == 0) {
		printf("Failed to load tilemap texture\n");
		return 1;
	}
	
	/* Create tilemap with 16x16 tiles */
	tm_ref = lam_tm_new(tex_ref, 16, 16);
	if (tm_ref == 0) {
		printf("Failed to create tilemap\n");
		return 1;
	}
	
	/* Get tilemap info */
	tm = lam_tm_get(tm_ref);
	if (tm) {
		printf("Tilemap: %u x %u tiles (%u x %u grid)\n", 
		       tm->w, tm->h, tm->nx, tm->ny);
	}
	
	/* Draw a simple tile pattern */
	int tile_size = 32;  /* Display tiles at 32x32 */
	
	/* Draw a row of tiles */
	for (int i = 0; i < 10; i++) {
		lam_tile_draw(tm_ref, i % 64, 50 + i * tile_size, 50, 
		              tile_size, tile_size, 0, 0);
	}
	
	/* Draw a grid pattern */
	for (int y = 0; y < 5; y++) {
		for (int x = 0; x < 8; x++) {
			int tile_idx = (y * 8 + x) % 64;
			lam_tile_draw(tm_ref, tile_idx, 
			              50 + x * tile_size, 
			              150 + y * tile_size,
			              tile_size, tile_size, 0, 0);
		}
	}
	
	/* Draw tiles with different scales */
	lam_tile_draw(tm_ref, 0, 50, 400, 64, 64, 0, 0);   /* 4x scale */
	lam_tile_draw(tm_ref, 1, 150, 400, 48, 48, 0, 0);  /* 3x scale */
	lam_tile_draw(tm_ref, 2, 230, 400, 32, 32, 0, 0);  /* 2x scale */
	lam_tile_draw(tm_ref, 3, 290, 400, 16, 16, 0, 0);  /* 1x scale */
	
	/* Present the frame */
	lam_flush();
	lam_poll();
	
	return 0;
}
