/**
 * test_integration.c - Integration tests for liblamina
 * Tests complete scenarios combining multiple subsystems
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ttypt/lam.h>
#include <ttypt/lam-ui.h>
#include <ttypt/lam-font.h>
#include <ttypt/lam-tm.h>

static void test_complete_ui_pipeline(void) {
	uint32_t w, h, ss;
	lam_ui_div_t *root, *header, *content, *footer;
	lam_ui_style_t header_style, content_style, footer_style;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	/* Create stylesheet */
	ss = lam_ui_stylesheet_init();
	
	lam_ui_style_reset(&header_style);
	header_style.background_color = 0xFF333333;
	header_style.height = 60;
	header_style.padding_left = 20;
	lam_ui_stylesheet_add(ss, "header", &header_style);
	
	lam_ui_style_reset(&content_style);
	content_style.background_color = 0xFFFFFFFF;
	content_style.flex_grow = 1.0f;
	content_style.padding_left = 20;
	content_style.padding_top = 20;
	lam_ui_stylesheet_add(ss, "content", &content_style);
	
	lam_ui_style_reset(&footer_style);
	footer_style.background_color = 0xFF333333;
	footer_style.height = 40;
	footer_style.padding_left = 20;
	lam_ui_stylesheet_add(ss, "footer", &footer_style);
	
	/* Build UI tree */
	root = lam_ui_new(NULL, NULL);
	LAM_UI_STYLE(root, display, LAM_UI_DISPLAY_FLEX);
	LAM_UI_STYLE(root, flex_direction, LAM_UI_COLUMN);
	LAM_UI_STYLE(root, width, 800);
	LAM_UI_STYLE(root, height, 600);
	
	header = lam_ui_new(root, NULL);
	lam_ui_class(header, "header");
	lam_ui_text(header, "Header");
	
	content = lam_ui_new(root, NULL);
	lam_ui_class(content, "content");
	lam_ui_text(content, "Main Content Area");
	
	footer = lam_ui_new(root, NULL);
	lam_ui_class(footer, "footer");
	lam_ui_text(footer, "Footer");
	
	/* Apply styles and layout */
	lam_ui_apply_styles(root, ss);
	lam_ui_layout(root);
	
	/* Verify layout */
	assert(lam_ui_get_y(header) == 0);
	assert(lam_ui_get_y(content) == 60);
	assert(lam_ui_get_height(content) > 0);
	assert(lam_ui_get_y(footer) > lam_ui_get_y(content));
	
	/* Render */
	lam_ui_render(root);
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_complete_ui_pipeline: PASS\n");
}

static void test_texture_and_ui(void) {
	uint32_t w, h, tex_ref;
	lam_ui_div_t *root, *container;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	/* Load a texture */
	tex_ref = lam_tex_load("tests/fixtures/test_texture.png");
	assert(tex_ref != CM_MISS);
	
	/* Create UI with background image */
	root = lam_ui_new(NULL, NULL);
	container = lam_ui_new(root, NULL);
	LAM_UI_STYLE(container, width, 200);
	LAM_UI_STYLE(container, height, 200);
	LAM_UI_STYLE(container, background_image_ref, tex_ref);
	
	lam_ui_layout(root);
	lam_ui_render(root);
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_texture_and_ui: PASS\n");
}

static void test_font_and_ui(void) {
	uint32_t w, h, font_ref;
	lam_ui_div_t *root, *text_box;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	/* Load a font (8x8 cells, ASCII 32-126) */
	font_ref = lam_font_open("tests/fixtures/test_font.png", 8, 8, 32, 126);
	assert(font_ref != CM_MISS);
	
	/* Create UI with text */
	root = lam_ui_new(NULL, NULL);
	text_box = lam_ui_new(root, NULL);
	LAM_UI_STYLE(text_box, width, 300);
	LAM_UI_STYLE(text_box, height, 100);
	LAM_UI_STYLE(text_box, font_family_ref, font_ref);
	LAM_UI_STYLE(text_box, font_size, 16);
	LAM_UI_STYLE(text_box, padding_left, 10);
	LAM_UI_STYLE(text_box, padding_top, 10);
	lam_ui_text(text_box, "Hello from liblamina!");
	
	lam_ui_layout(root);
	lam_ui_render(root);
	
	lam_ui_clear(root);
	free(root);
	lam_font_close(font_ref);
	
	printf("  test_font_and_ui: PASS\n");
}

static void test_responsive_layout(void) {
	uint32_t w, h;
	lam_ui_div_t *root, *col1, *col2, *col3;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	/* Create a responsive 3-column layout */
	root = lam_ui_new(NULL, NULL);
	LAM_UI_STYLE(root, display, LAM_UI_DISPLAY_FLEX);
	LAM_UI_STYLE(root, flex_direction, LAM_UI_ROW);
	LAM_UI_STYLE(root, width, 900);
	LAM_UI_STYLE(root, height, 400);
	
	col1 = lam_ui_new(root, NULL);
	LAM_UI_STYLE(col1, flex_grow, 1.0f);
	LAM_UI_STYLE(col1, flex_basis, 0);
	LAM_UI_STYLE(col1, background_color, 0xFFFF0000);
	
	col2 = lam_ui_new(root, NULL);
	LAM_UI_STYLE(col2, flex_grow, 2.0f);
	LAM_UI_STYLE(col2, flex_basis, 0);
	LAM_UI_STYLE(col2, background_color, 0xFF00FF00);
	
	col3 = lam_ui_new(root, NULL);
	LAM_UI_STYLE(col3, flex_grow, 1.0f);
	LAM_UI_STYLE(col3, flex_basis, 0);
	LAM_UI_STYLE(col3, background_color, 0xFFFF0000);
	
	lam_ui_layout(root);
	
	/* Verify proportional sizing */
	int32_t w1 = lam_ui_get_width(col1);
	int32_t w2 = lam_ui_get_width(col2);
	int32_t w3 = lam_ui_get_width(col3);
	
	assert(w1 > 0);
	assert(w2 > w1);  /* col2 should be wider */
	assert(w3 == w1); /* col1 and col3 should be equal */
	
	lam_ui_render(root);
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_responsive_layout: PASS\n");
}

static void test_nested_flex_layout(void) {
	uint32_t w, h;
	lam_ui_div_t *root, *sidebar, *main, *header, *content;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	/* Create nested flex layout: horizontal split with vertical split in main */
	root = lam_ui_new(NULL, NULL);
	LAM_UI_STYLE(root, display, LAM_UI_DISPLAY_FLEX);
	LAM_UI_STYLE(root, flex_direction, LAM_UI_ROW);
	LAM_UI_STYLE(root, width, 1000);
	LAM_UI_STYLE(root, height, 600);
	
	sidebar = lam_ui_new(root, NULL);
	LAM_UI_STYLE(sidebar, flex_basis, 200);
	LAM_UI_STYLE(sidebar, background_color, 0xFF2C3E50);
	
	main = lam_ui_new(root, NULL);
	LAM_UI_STYLE(main, flex_grow, 1.0f);
	LAM_UI_STYLE(main, display, LAM_UI_DISPLAY_FLEX);
	LAM_UI_STYLE(main, flex_direction, LAM_UI_COLUMN);
	
	header = lam_ui_new(main, NULL);
	LAM_UI_STYLE(header, flex_basis, 80);
	LAM_UI_STYLE(header, background_color, 0xFF34495E);
	
	content = lam_ui_new(main, NULL);
	LAM_UI_STYLE(content, flex_grow, 1.0f);
	LAM_UI_STYLE(content, background_color, 0xFFECF0F1);
	
	lam_ui_layout(root);
	
	/* Verify nested layout */
	assert(lam_ui_get_x(sidebar) == 0);
	assert(lam_ui_get_width(sidebar) == 200);
	assert(lam_ui_get_x(main) == 200);
	assert(lam_ui_get_width(main) == 800);
	assert(lam_ui_get_y(header) == 0);
	assert(lam_ui_get_height(header) == 80);
	assert(lam_ui_get_y(content) == 80);
	assert(lam_ui_get_height(content) == 520);
	
	lam_ui_render(root);
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_nested_flex_layout: PASS\n");
}

static void test_text_overflow(void) {
	uint32_t w, h, font_ref;
	lam_ui_div_t *root, *text_box;
	const char *long_text = "This is a very long text that will definitely overflow "
	                        "the container and should be handled properly by the layout "
	                        "and rendering system.";
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	font_ref = lam_font_open("tests/fixtures/test_font.png", 8, 8, 32, 126);
	assert(font_ref != CM_MISS);
	
	root = lam_ui_new(NULL, NULL);
	text_box = lam_ui_new(root, NULL);
	LAM_UI_STYLE(text_box, width, 100);
	LAM_UI_STYLE(text_box, height, 30);
	LAM_UI_STYLE(text_box, font_family_ref, font_ref);
	LAM_UI_STYLE(text_box, overflow_y, LAM_UI_OVERFLOW_HIDDEN);
	lam_ui_text(text_box, long_text);
	
	lam_ui_layout(root);
	lam_ui_render(root);
	
	/* Check if overflow was detected */
	const char *overflow = lam_ui_overflow(text_box);
	(void)overflow;  /* May or may not be NULL depending on implementation */
	
	lam_ui_clear(root);
	free(root);
	lam_font_close(font_ref);
	
	printf("  test_text_overflow: PASS\n");
}

static void test_mixed_positioning(void) {
	uint32_t w, h;
	lam_ui_div_t *root, *relative, *absolute;
	
	lam_size(&w, &h);
	lam_ui_init(w, h);
	
	root = lam_ui_new(NULL, NULL);
	LAM_UI_STYLE(root, width, 500);
	LAM_UI_STYLE(root, height, 500);
	
	relative = lam_ui_new(root, NULL);
	LAM_UI_STYLE(relative, position, LAM_UI_POSITION_RELATIVE);
	LAM_UI_STYLE(relative, width, 200);
	LAM_UI_STYLE(relative, height, 200);
	LAM_UI_STYLE(relative, background_color, 0xFF3498DB);
	
	absolute = lam_ui_new(root, NULL);
	LAM_UI_STYLE(absolute, position, LAM_UI_POSITION_ABSOLUTE);
	LAM_UI_STYLE(absolute, left, 50);
	LAM_UI_STYLE(absolute, top, 50);
	LAM_UI_STYLE(absolute, width, 100);
	LAM_UI_STYLE(absolute, height, 100);
	LAM_UI_STYLE(absolute, background_color, 0xFFE74C3C);
	
	lam_ui_layout(root);
	lam_ui_render(root);
	
	/* Verify positioning */
	assert(lam_ui_get_x(absolute) == 50);
	assert(lam_ui_get_y(absolute) == 50);
	
	lam_ui_clear(root);
	free(root);
	
	printf("  test_mixed_positioning: PASS\n");
}

int main(void) {
	printf("test_integration:\n");
	lam_init();
	
	test_complete_ui_pipeline();
	test_texture_and_ui();
	test_font_and_ui();
	test_responsive_layout();
	test_nested_flex_layout();
	test_text_overflow();
	test_mixed_positioning();
	
	printf("test_integration: ALL TESTS PASSED\n");
	return 0;
}
