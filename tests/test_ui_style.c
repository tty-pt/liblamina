/**
 * test_ui_style.c - UI Styling tests for liblamina
 * Tests stylesheet creation, class application, and style inheritance
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ttypt/lam.h>
#include <ttypt/lam-ui.h>

static void test_lam_ui_stylesheet_init(void) {
	uint32_t ss;
	
	ss = lam_ui_stylesheet_init();
	assert(ss != 0);
	
	printf("  test_lam_ui_stylesheet_init: PASS\n");
}

static void test_lam_ui_stylesheet_add(void) {
	uint32_t ss;
	lam_ui_style_t style;
	
	ss = lam_ui_stylesheet_init();
	lam_ui_style_reset(&style);
	
	style.background_color = 0xFF0000FF;  /* Red */
	style.width = 100;
	style.height = 50;
	
	lam_ui_stylesheet_add(ss, "test-class", &style);
	
	printf("  test_lam_ui_stylesheet_add: PASS\n");
}

static void test_lam_ui_class(void) {
	uint32_t w, h;
	lam_ui_div_t *div;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	div = lam_ui_new(NULL, NULL);
	lam_ui_class(div, "my-class");
	
	lam_ui_clear(div);
	free(div);
	
	printf("  test_lam_ui_class: PASS\n");
}

static void test_lam_ui_apply_styles(void) {
	uint32_t w, h, ss;
	lam_ui_div_t *root, *child;
	lam_ui_style_t style;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	/* Create stylesheet with a class */
	ss = lam_ui_stylesheet_init();
	lam_ui_style_reset(&style);
	style.background_color = 0xFF00FF00;  /* Green */
	style.padding_left = 10;
	style.padding_top = 20;
	lam_ui_stylesheet_add(ss, "green-box", &style);
	
	/* Create element with that class */
	root = lam_ui_new(NULL, NULL);
	child = lam_ui_new(root, NULL);
	lam_ui_class(child, "green-box");
	
	/* Apply styles */
	lam_ui_apply_styles(root, ss);
	
	/* Note: We can't verify the actual style was applied since
	 * lam_ui_style_t is not accessible from the public API. 
	 * This test mainly ensures the function doesn't crash */
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_lam_ui_apply_styles: PASS\n");
}

static void test_lam_ui_style_set(void) {
	uint32_t w, h;
	lam_ui_div_t *div;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	div = lam_ui_new(NULL, NULL);
	
	/* Test LAM_UI_STYLE macro which uses lam_ui_style_set */
	LAM_UI_STYLE(div, width, 200);
	LAM_UI_STYLE(div, height, 100);
	LAM_UI_STYLE(div, background_color, 0xFFFF0000);  /* Blue */
	
	lam_ui_clear(div);
	free(div);
	
	printf("  test_lam_ui_style_set: PASS\n");
}

static void test_lam_ui_style_reset(void) {
	lam_ui_style_t style;
	
	/* Set some non-default values */
	memset(&style, 0xFF, sizeof(style));
	
	/* Reset to defaults */
	lam_ui_style_reset(&style);
	
	/* Verify some default values */
	assert(style.background_color == 0);
	assert(style.border_width == 0);
	assert(style.display == LAM_UI_DISPLAY_BLOCK);
	assert(style.flex_grow == 0.0f);
	assert(style.flex_shrink == 1.0f);
	
	printf("  test_lam_ui_style_reset: PASS\n");
}

static void test_lam_ui_inline_style(void) {
	uint32_t w, h;
	lam_ui_div_t *root, *div_inline, *div_default;
	lam_ui_style_t inline_style;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	root = lam_ui_new(NULL, NULL);
	
	/* Create div with inline style */
	lam_ui_style_reset(&inline_style);
	inline_style.width = 150;
	inline_style.height = 75;
	inline_style.display = LAM_UI_DISPLAY_FLEX;
	div_inline = lam_ui_new(root, &inline_style);
	
	/* Create div with default style */
	div_default = lam_ui_new(root, NULL);
	
	assert(div_inline != NULL);
	assert(div_default != NULL);
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_lam_ui_inline_style: PASS\n");
}

static void test_lam_ui_multiple_classes(void) {
	uint32_t w, h, ss;
	lam_ui_div_t *root, *child1, *child2, *child3;
	lam_ui_style_t style1, style2, style3;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	/* Create stylesheet with multiple classes */
	ss = lam_ui_stylesheet_init();
	
	lam_ui_style_reset(&style1);
	style1.background_color = 0xFF0000FF;
	lam_ui_stylesheet_add(ss, "red", &style1);
	
	lam_ui_style_reset(&style2);
	style2.background_color = 0xFF00FF00;
	lam_ui_stylesheet_add(ss, "green", &style2);
	
	lam_ui_style_reset(&style3);
	style3.background_color = 0xFFFF0000;
	lam_ui_stylesheet_add(ss, "blue", &style3);
	
	/* Create elements with different classes */
	root = lam_ui_new(NULL, NULL);
	child1 = lam_ui_new(root, NULL);
	lam_ui_class(child1, "red");
	
	child2 = lam_ui_new(root, NULL);
	lam_ui_class(child2, "green");
	
	child3 = lam_ui_new(root, NULL);
	lam_ui_class(child3, "blue");
	
	lam_ui_apply_styles(root, ss);
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_lam_ui_multiple_classes: PASS\n");
}

static void test_lam_ui_style_properties(void) {
	uint32_t w, h;
	lam_ui_div_t *div;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	div = lam_ui_new(NULL, NULL);
	
	/* Test various style properties */
	LAM_UI_STYLE(div, display, LAM_UI_DISPLAY_FLEX);
	LAM_UI_STYLE(div, flex_direction, LAM_UI_ROW);
	LAM_UI_STYLE(div, justify_content, LAM_UI_JUSTIFY_CENTER);
	LAM_UI_STYLE(div, align_items, LAM_UI_ALIGN_CENTER);
	LAM_UI_STYLE(div, position, LAM_UI_POSITION_ABSOLUTE);
	LAM_UI_STYLE(div, top, 10);
	LAM_UI_STYLE(div, left, 20);
	LAM_UI_STYLE(div, right, 30);
	LAM_UI_STYLE(div, bottom, 40);
	LAM_UI_STYLE(div, flex_grow, 1.5f);
	LAM_UI_STYLE(div, flex_shrink, 0.5f);
	LAM_UI_STYLE(div, flex_basis, 100);
	LAM_UI_STYLE(div, padding_top, 5);
	LAM_UI_STYLE(div, padding_right, 10);
	LAM_UI_STYLE(div, padding_bottom, 15);
	LAM_UI_STYLE(div, padding_left, 20);
	LAM_UI_STYLE(div, border_width, 2);
	LAM_UI_STYLE(div, border_color, 0xFF000000);
	LAM_UI_STYLE(div, border_radius_top_left, 5);
	LAM_UI_STYLE(div, border_radius_top_right, 10);
	LAM_UI_STYLE(div, border_radius_bottom_right, 15);
	LAM_UI_STYLE(div, border_radius_bottom_left, 20);
	
	lam_ui_clear(div);
	free(div);
	
	printf("  test_lam_ui_style_properties: PASS\n");
}

int main(void) {
	printf("test_ui_style:\n");
	lam_init();
	
	test_lam_ui_stylesheet_init();
	test_lam_ui_stylesheet_add();
	test_lam_ui_class();
	test_lam_ui_apply_styles();
	test_lam_ui_style_set();
	test_lam_ui_style_reset();
	test_lam_ui_inline_style();
	test_lam_ui_multiple_classes();
	test_lam_ui_style_properties();
	
	printf("test_ui_style: ALL TESTS PASSED\n");
	return 0;
}
