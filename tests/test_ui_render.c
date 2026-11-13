/**
 * test_ui_render.c - UI Rendering tests for liblamina
 * Tests rendering, caching, and visual output
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ttypt/lam.h>
#include <ttypt/lam-ui.h>
#include <ttypt/lam-font.h>

static void test_lam_ui_render_basic(void) {
	uint32_t w, h;
	lam_ui_div_t *root;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	root = lam_ui_new(NULL, NULL);
	LAM_UI_STYLE(root, width, 100);
	LAM_UI_STYLE(root, height, 100);
	LAM_UI_STYLE(root, background_color, 0xFF0000FF);
	
	lam_ui_layout(root);
	
	/* Render should not crash */
	lam_ui_render(root);
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_lam_ui_render_basic: PASS\n");
}

static void test_lam_ui_render_text(void) {
	uint32_t w, h;
	lam_ui_div_t *root;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	root = lam_ui_new(NULL, NULL);
	LAM_UI_STYLE(root, width, 200);
	LAM_UI_STYLE(root, height, 50);
	lam_ui_text(root, "Hello World");
	
	lam_ui_layout(root);
	lam_ui_render(root);
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_lam_ui_render_text: PASS\n");
}

static void test_lam_ui_render_nested(void) {
	uint32_t w, h;
	lam_ui_div_t *root, *child1, *child2;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	root = lam_ui_new(NULL, NULL);
	LAM_UI_STYLE(root, display, LAM_UI_DISPLAY_FLEX);
	LAM_UI_STYLE(root, width, 400);
	LAM_UI_STYLE(root, height, 200);
	
	child1 = lam_ui_new(root, NULL);
	LAM_UI_STYLE(child1, flex_basis, 100);
	LAM_UI_STYLE(child1, background_color, 0xFF0000FF);
	
	child2 = lam_ui_new(root, NULL);
	LAM_UI_STYLE(child2, flex_basis, 100);
	LAM_UI_STYLE(child2, background_color, 0xFF00FF00);
	
	lam_ui_layout(root);
	lam_ui_render(root);
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_lam_ui_render_nested: PASS\n");
}

static void test_lam_ui_mark_dirty(void) {
	uint32_t w, h;
	lam_ui_div_t *root, *child;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	root = lam_ui_new(NULL, NULL);
	child = lam_ui_new(root, NULL);
	
	lam_ui_layout(root);
	lam_ui_render(root);
	
	/* Mark dirty and re-render */
	lam_ui_mark_dirty(child);
	lam_ui_layout(root);
	lam_ui_render(root);
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_lam_ui_mark_dirty: PASS\n");
}

static void test_lam_ui_render_border(void) {
	uint32_t w, h;
	lam_ui_div_t *root;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	root = lam_ui_new(NULL, NULL);
	LAM_UI_STYLE(root, width, 100);
	LAM_UI_STYLE(root, height, 100);
	LAM_UI_STYLE(root, background_color, 0xFFFFFFFF);
	LAM_UI_STYLE(root, border_width, 5);
	LAM_UI_STYLE(root, border_color, 0xFF000000);
	
	lam_ui_layout(root);
	lam_ui_render(root);
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_lam_ui_render_border: PASS\n");
}

static void test_lam_ui_render_border_radius(void) {
	uint32_t w, h;
	lam_ui_div_t *root;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	root = lam_ui_new(NULL, NULL);
	LAM_UI_STYLE(root, width, 100);
	LAM_UI_STYLE(root, height, 100);
	LAM_UI_STYLE(root, background_color, 0xFFFF0000);
	LAM_UI_STYLE(root, border_width, 2);
	LAM_UI_STYLE(root, border_color, 0xFF000000);
	LAM_UI_STYLE(root, border_radius_top_left, 10);
	LAM_UI_STYLE(root, border_radius_top_right, 10);
	LAM_UI_STYLE(root, border_radius_bottom_right, 10);
	LAM_UI_STYLE(root, border_radius_bottom_left, 10);
	
	lam_ui_layout(root);
	lam_ui_render(root);
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_lam_ui_render_border_radius: PASS\n");
}

static void test_lam_ui_render_padding(void) {
	uint32_t w, h;
	lam_ui_div_t *root, *child;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	root = lam_ui_new(NULL, NULL);
	LAM_UI_STYLE(root, width, 200);
	LAM_UI_STYLE(root, height, 200);
	LAM_UI_STYLE(root, background_color, 0xFFCCCCCC);
	LAM_UI_STYLE(root, padding_top, 20);
	LAM_UI_STYLE(root, padding_right, 20);
	LAM_UI_STYLE(root, padding_bottom, 20);
	LAM_UI_STYLE(root, padding_left, 20);
	
	child = lam_ui_new(root, NULL);
	LAM_UI_STYLE(child, width, 100);
	LAM_UI_STYLE(child, height, 100);
	LAM_UI_STYLE(child, background_color, 0xFF0000FF);
	
	lam_ui_layout(root);
	lam_ui_render(root);
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_lam_ui_render_padding: PASS\n");
}

static void test_lam_ui_render_box_shadow(void) {
	uint32_t w, h;
	lam_ui_div_t *root;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	root = lam_ui_new(NULL, NULL);
	LAM_UI_STYLE(root, width, 100);
	LAM_UI_STYLE(root, height, 100);
	LAM_UI_STYLE(root, background_color, 0xFFFFFFFF);
	LAM_UI_STYLE(root, box_shadow_color, 0x80000000);  /* Semi-transparent black */
	LAM_UI_STYLE(root, box_shadow_blur, 10.0f);
	LAM_UI_STYLE(root, box_shadow_offset_x, 5.0f);
	LAM_UI_STYLE(root, box_shadow_offset_y, 5.0f);
	
	lam_ui_layout(root);
	lam_ui_render(root);
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_lam_ui_render_box_shadow: PASS\n");
}

static void test_lam_ui_render_display_none(void) {
	uint32_t w, h;
	lam_ui_div_t *root, *hidden_child, *visible_child;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	root = lam_ui_new(NULL, NULL);
	LAM_UI_STYLE(root, display, LAM_UI_DISPLAY_FLEX);
	
	hidden_child = lam_ui_new(root, NULL);
	LAM_UI_STYLE(hidden_child, display, LAM_UI_DISPLAY_NONE);
	LAM_UI_STYLE(hidden_child, width, 100);
	LAM_UI_STYLE(hidden_child, height, 100);
	
	visible_child = lam_ui_new(root, NULL);
	LAM_UI_STYLE(visible_child, width, 100);
	LAM_UI_STYLE(visible_child, height, 100);
	LAM_UI_STYLE(visible_child, background_color, 0xFF00FF00);
	
	lam_ui_layout(root);
	lam_ui_render(root);
	
	/* Hidden child should not affect layout */
	assert(lam_ui_get_x(visible_child) == 0);
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_lam_ui_render_display_none: PASS\n");
}

static void test_lam_ui_clear(void) {
	uint32_t w, h;
	lam_ui_div_t *root, *child1, *child2;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	root = lam_ui_new(NULL, NULL);
	child1 = lam_ui_new(root, NULL);
	child2 = lam_ui_new(root, NULL);
	
	/* Add grandchildren */
	lam_ui_new(child1, NULL);
	lam_ui_new(child1, NULL);
	lam_ui_new(child2, NULL);
	
	/* Clear should recursively free all children */
	lam_ui_clear(root);
	free(root);
	
	printf("  test_lam_ui_clear: PASS\n");
}

int main(void) {
	printf("test_ui_render:\n");
	lam_init();
	
	test_lam_ui_render_basic();
	test_lam_ui_render_text();
	test_lam_ui_render_nested();
	test_lam_ui_mark_dirty();
	test_lam_ui_render_border();
	test_lam_ui_render_border_radius();
	test_lam_ui_render_padding();
	test_lam_ui_render_box_shadow();
	test_lam_ui_render_display_none();
	test_lam_ui_clear();
	
	printf("test_ui_render: ALL TESTS PASSED\n");
	return 0;
}
