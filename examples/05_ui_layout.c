/**
 * 05_ui_layout.c - UI layout system example
 * 
 * Demonstrates:
 * - Creating a UI element tree with declarative helpers (lam_ui_flex, lam_ui_box)
 * - Using flexbox layout (row and column)
 * - Setting element dimensions and styles with direct setters
 * - Computing layout with lam_ui_layout()
 * - Rendering UI elements with lam_ui_render()
 * 
 * To compile:
 *   cc -o 05_ui_layout 05_ui_layout.c -llamina
 */

#include <ttypt/lam.h>
#include <ttypt/lam-ui.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	uint32_t w, h;
	lam_ui_div_t *root, *header, *content, *footer;

	/* Initialize and get screen dimensions */
	lam_init();
	lam_size(&w, &h);
	lam_ui_init(w, h);

	/* Clear background */
	lam_clear(LAM_COLOR_BLACK);

	/* Create root container with flexbox column layout in one call */
	root = lam_ui_flex(NULL, LAM_UI_COLUMN);
	lam_ui_set_size(root, 600, 400);

	/* Create header with declarative helper and setter */
	header = lam_ui_box(root, LAM_UI_AUTO, 60, LAM_RGB(74, 74, 74));
	lam_ui_set_padding(header, 20, 20, 20, 0);
	lam_ui_text(header, "Header");

	/* Create content area (grows to fill space) */
	content = lam_ui_box(root, LAM_UI_AUTO, LAM_UI_AUTO, LAM_RGB(238, 238, 238));
	lam_ui_set_flex(content, 1.0f, 1.0f, 0);
	lam_ui_set_padding(content, 20, 20, 20, 0);
	lam_ui_text(content, "Content Area");

	/* Create footer */
	footer = lam_ui_box(root, LAM_UI_AUTO, 40, LAM_RGB(74, 74, 74));
	lam_ui_set_padding(footer, 20, 10, 20, 0);
	lam_ui_text(footer, "Footer");

	/* Compute layout */
	lam_ui_layout(root);

	/* Print computed positions */
	printf("Header: x=%d y=%d w=%d h=%d\n",
	       lam_ui_get_x(header), lam_ui_get_y(header),
	       lam_ui_get_width(header), lam_ui_get_height(header));
	printf("Content: x=%d y=%d w=%d h=%d\n",
	       lam_ui_get_x(content), lam_ui_get_y(content),
	       lam_ui_get_width(content), lam_ui_get_height(content));
	printf("Footer: x=%d y=%d w=%d h=%d\n",
	       lam_ui_get_x(footer), lam_ui_get_y(footer),
	       lam_ui_get_width(footer), lam_ui_get_height(footer));

	/* Render the UI tree */
	lam_ui_render(root);

	/* Clean up */
	lam_ui_clear(root);
	free(root);

	/* Present frame */
	lam_flush();
	lam_poll();

	return 0;
}
