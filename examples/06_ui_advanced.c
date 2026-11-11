/**
 * 06_ui_advanced.c - Advanced UI with styling and nested layouts
 * 
 * Demonstrates:
 * - Creating a stylesheet with lam_ui_stylesheet_init()
 * - Defining style classes with lam_ui_stylesheet_add()
 * - Applying classes to elements with lam_ui_class()
 * - Nested flexbox layouts (sidebar + main area)
 * - Border radius and box shadows
 * - Responsive layouts with flex_grow
 * 
 * To compile:
 *   cc -o 06_ui_advanced 06_ui_advanced.c -llamina
 */

#include <ttypt/lam.h>
#include <ttypt/lam-ui.h>
#include <ttypt/lam-font.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	uint32_t w, h;
	uint32_t ss;
	lam_ui_div_t *root, *sidebar, *main_area, *header, *content;
	lam_ui_style_t sidebar_style, header_style, content_style, button_style;
	
	/* Get screen dimensions */
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	/* Fill background */
	lam_fill(0, 0, w, h, 0xFF0A0A0A);
	
	/* Create stylesheet */
	ss = lam_ui_stylesheet_init();
	
	/* Define sidebar style */
	lam_ui_style_reset(&sidebar_style);
	sidebar_style.background_color = 0xFF2C3E50;
	sidebar_style.padding_top = 20;
	sidebar_style.padding_left = 15;
	sidebar_style.flex_basis = 200;
	lam_ui_stylesheet_add(ss, "sidebar", &sidebar_style);
	
	/* Define header style */
	lam_ui_style_reset(&header_style);
	header_style.background_color = 0xFF34495E;
	header_style.height = 80;
	header_style.padding_top = 25;
	header_style.padding_left = 30;
	header_style.box_shadow_color = 0x40000000;
	header_style.box_shadow_blur = 8.0f;
	header_style.box_shadow_offset_y = 4.0f;
	lam_ui_stylesheet_add(ss, "header", &header_style);
	
	/* Define content style */
	lam_ui_style_reset(&content_style);
	content_style.background_color = 0xFFECF0F1;
	content_style.flex_grow = 1.0f;
	content_style.padding_top = 30;
	content_style.padding_left = 30;
	lam_ui_stylesheet_add(ss, "content", &content_style);
	
	/* Define button style */
	lam_ui_style_reset(&button_style);
	button_style.background_color = 0xFF3498DB;
	button_style.width = 120;
	button_style.height = 40;
	button_style.border_radius_top_left = 5;
	button_style.border_radius_top_right = 5;
	button_style.border_radius_bottom_right = 5;
	button_style.border_radius_bottom_left = 5;
	button_style.padding_left = 15;
	button_style.padding_top = 10;
	button_style.box_shadow_color = 0x60000000;
	button_style.box_shadow_blur = 5.0f;
	button_style.box_shadow_offset_y = 2.0f;
	lam_ui_stylesheet_add(ss, "button", &button_style);
	
	/* Create root container (horizontal split) */
	root = lam_ui_new(NULL, NULL);
	LAM_UI_STYLE(root, display, LAM_UI_DISPLAY_FLEX);
	LAM_UI_STYLE(root, flex_direction, LAM_UI_ROW);
	LAM_UI_STYLE(root, width, 800);
	LAM_UI_STYLE(root, height, 600);
	
	/* Create sidebar */
	sidebar = lam_ui_new(root, NULL);
	lam_ui_class(sidebar, "sidebar");
	lam_ui_text(sidebar, "Navigation");
	
	/* Create main area (vertical split) */
	main_area = lam_ui_new(root, NULL);
	LAM_UI_STYLE(main_area, display, LAM_UI_DISPLAY_FLEX);
	LAM_UI_STYLE(main_area, flex_direction, LAM_UI_COLUMN);
	LAM_UI_STYLE(main_area, flex_grow, 1.0f);
	
	/* Create header */
	header = lam_ui_new(main_area, NULL);
	lam_ui_class(header, "header");
	lam_ui_text(header, "Dashboard");
	
	/* Create content area */
	content = lam_ui_new(main_area, NULL);
	lam_ui_class(content, "content");
	lam_ui_text(content, "Main Content");
	
	/* Add some button-like elements to content */
	lam_ui_div_t *button1 = lam_ui_new(content, NULL);
	lam_ui_class(button1, "button");
	lam_ui_text(button1, "Click Me");
	
	lam_ui_div_t *button2 = lam_ui_new(content, NULL);
	lam_ui_class(button2, "button");
	lam_ui_text(button2, "Action");
	LAM_UI_STYLE(button2, background_color, 0xFF2ECC71);  /* Green button */
	
	/* Apply styles from stylesheet */
	lam_ui_apply_styles(root, ss);
	
	/* Compute layout */
	lam_ui_layout(root);
	
	/* Print layout info */
	printf("Sidebar: %dx%d at (%d,%d)\n",
	       lam_ui_get_width(sidebar), lam_ui_get_height(sidebar),
	       lam_ui_get_x(sidebar), lam_ui_get_y(sidebar));
	printf("Header: %dx%d at (%d,%d)\n",
	       lam_ui_get_width(header), lam_ui_get_height(header),
	       lam_ui_get_x(header), lam_ui_get_y(header));
	printf("Content: %dx%d at (%d,%d)\n",
	       lam_ui_get_width(content), lam_ui_get_height(content),
	       lam_ui_get_x(content), lam_ui_get_y(content));
	
	/* Render UI */
	lam_ui_render(root);
	
	/* Clean up */
	lam_ui_clear(root);
	free(root);
	
	/* Present the frame */
	lam_flush();
	lam_poll();
	
	return 0;
}
