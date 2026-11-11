/**
 * 01_basic_rendering.c - Basic liblamina rendering example
 * 
 * Demonstrates:
 * - Initializing liblamina and getting screen dimensions
 * - Clearing the screen with lam_clear()
 * - Filling rectangles with LAM_RGB() and LAM_RGBA()
 * - Drawing outline rectangles with lam_rect()
 * - Presenting the frame with lam_flush()
 * 
 * To compile:
 *   cc -o 01_basic_rendering 01_basic_rendering.c -llamina
 */

#include <ttypt/lam.h>
#include <stdio.h>

int main(void)
{
	uint32_t w, h;

	/* Initialize display and get dimensions */
	lam_init();
	lam_size(&w, &h);
	printf("Window size: %u x %u\n", w, h);

	/* Clear background with dark gray using LAM_RGB */
	lam_clear(LAM_RGB(32, 32, 32));

	/* Draw solid rectangles using color macros */
	lam_fill(50, 50, 200, 100, LAM_COLOR_RED);
	lam_fill(100, 200, 150, 80, LAM_COLOR_GREEN);
	lam_fill(300, 150, 180, 120, LAM_COLOR_BLUE);

	/* Draw semi-transparent rectangle using LAM_RGBA */
	lam_fill(200, 300, 250, 100, LAM_RGBA(255, 255, 0, 128));

	/* Draw outline rectangle and lines */
	lam_rect(45, 45, 210, 110, LAM_COLOR_WHITE, 2);
	lam_line(50, 450, 450, 450, LAM_COLOR_YELLOW, 3);

	/* Present frame and poll events */
	lam_flush();
	lam_poll();

	return 0;
}
