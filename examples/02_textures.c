/**
 * 02_textures.c - Texture loading and drawing example
 * 
 * Demonstrates:
 * - Loading textures from PNG files
 * - Drawing textures at specific positions
 * - Scaling textures
 * - Applying tints to textures
 * - Querying texture dimensions
 * 
 * To compile:
 *   cc -o 02_textures 02_textures.c -llamina
 * 
 * Note: Requires a PNG image file (e.g., ../tests/fixtures/test_texture.png)
 */

#include <ttypt/lam.h>
#include <stdio.h>

int main(void)
{
	uint32_t w, h;
	uint32_t tex_ref;
	uint32_t tex_w, tex_h;
	
	/* Get screen dimensions */
	lam_size(&w, &h);
	
	/* Fill background */
	lam_fill(0, 0, w, h, 0xFF1A1A1A);
	
	/* Load a texture */
	tex_ref = lam_tex_load("../tests/fixtures/test_texture.png");
	if (tex_ref == 0) {
		printf("Failed to load texture\n");
		return 1;
	}
	
	/* Query texture size */
	lam_tex_size(&tex_w, &tex_h, tex_ref);
	printf("Texture size: %u x %u\n", tex_w, tex_h);
	
	/* Draw texture at original size */
	lam_tex_draw(tex_ref, 50, 50, tex_w, tex_h);
	
	/* Draw texture scaled 2x */
	lam_tex_draw(tex_ref, 200, 50, tex_w * 2, tex_h * 2);
	
	/* Apply a red tint and draw */
	lam_tint(0xFF0000FF);  /* Red tint */
	lam_tex_draw(tex_ref, 50, 200, tex_w, tex_h);
	
	/* Apply a green tint and draw */
	lam_tint(0xFF00FF00);  /* Green tint */
	lam_tex_draw(tex_ref, 200, 200, tex_w, tex_h);
	
	/* Reset tint to default (white = no tint) */
	lam_tint(lam_default_tint);
	
	/* Draw texture with 50% transparency */
	lam_tint(0x80FFFFFF);  /* 50% transparent white */
	lam_tex_draw(tex_ref, 350, 200, tex_w, tex_h);
	
	/* Reset tint */
	lam_tint(lam_default_tint);
	
	/* Present the frame */
	lam_flush();
	lam_poll();
	
	return 0;
}
