/**
 * 03_fonts.c - Bitmap font rendering example
 * 
 * Demonstrates:
 * - Loading bitmap fonts from PNG atlases
 * - Rendering text at different positions
 * - Measuring text dimensions
 * - Text wrapping and overflow handling
 * - Using different font sizes
 * 
 * To compile:
 *   cc -o 03_fonts 03_fonts.c -llamina
 * 
 * Note: Requires a font atlas PNG (e.g., ../tests/fixtures/test_font.png)
 */

#include <ttypt/lam.h>
#include <ttypt/lam-font.h>
#include <ttypt/lam-ui.h>
#include <stdio.h>

int main(void)
{
	uint32_t w, h;
	uint32_t font_ref;
	uint32_t text_w, text_h;
	const char *text = "Hello, liblamina!";
	const char *long_text = "This is a longer text that demonstrates text "
	                        "wrapping and overflow handling in the liblamina font system.";
	
	/* Get screen dimensions */
	lam_size(&w, &h);
	
	/* Fill background */
	lam_fill(0, 0, w, h, 0xFF1E1E1E);
	
	/* Load a bitmap font (8x8 cells, ASCII 32-126) */
	font_ref = lam_font_open("../tests/fixtures/test_font.png", 8, 8, 32, 126);
	if (font_ref == 0) {
		printf("Failed to load font\n");
		return 1;
	}
	
	/* Measure text - bounding box (0,0) to (w,h) */
	lam_font_measure(&text_w, &text_h, font_ref, text, 
	                 0, 0, w, h, 8, LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	printf("Text '%s' dimensions: %u x %u\n", text, text_w, text_h);
	
	/* Draw text at normal size (8x8) - bounding box from (50,50) to (w,h) */
	lam_font_draw(font_ref, text, 50, 50, w, h, 8, LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	
	/* Draw text at 2x size (16x16) */
	lam_font_draw(font_ref, "Large Text", 50, 100, w, h, 16, LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	
	/* Draw text at smaller size (6x6) */
	lam_font_draw(font_ref, "Small Text", 50, 150, w, h, 6, LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	
	/* Draw text with color tint */
	lam_tint(0xFF00FFFF);  /* Yellow */
	lam_font_draw(font_ref, "Colored Text", 50, 200, w, h, 8, LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	lam_tint(lam_default_tint);
	
	/* Demonstrate text wrapping - constrain width to 300px */
	lam_font_draw(font_ref, long_text, 50, 250, 350, h, 8, LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	
	/* Measure wrapped text - constrain width to 300px */
	lam_font_measure(&text_w, &text_h, font_ref, long_text,
	                 50, 250, 350, h, 8, LAM_UI_WS_NORMAL, LAM_UI_WB_NORMAL);
	printf("Wrapped text dimensions: %u x %u\n", text_w, text_h);
	
	/* Clean up */
	lam_font_close(font_ref);
	
	/* Present the frame */
	lam_flush();
	lam_poll();
	
	return 0;
}
