/**
 * test_css_parser.c — Unit tests for runtime CSS parser, cascade,
 * extended layout engine, z-index hit-testing, and real NeverDark CSS files.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ttypt/lam.h>
#include <ttypt/lam-font.h>
#include <ttypt/lam-ui.h>

static void test_specificity_important_and_hover(void)
{
	lam_ui_init(1280, 720);
	uint32_t ss = lam_ui_stylesheet_init();

	const char *css =
		":root { --accent: #336699; }\n"
		"button { background-color: #111111; color: #ffffff; padding: 4px 8px; }\n"
		".btn { background-color: var(--accent); }\n"
		"#go { background-color: #22aa44; }\n"
		".btn:hover { background-color: #ff8800; }\n"
		".forced { color: #ff0000 !important; }\n"
		"#go.forced { color: #00ff00; }\n"
		".mix { background-color: color-mix(in srgb, #2c2c2c, transparent 50%); }\n"
		".mr-\\[8px\\] { margin-right: 8px; }\n";

	int n = lam_css_parse(ss, css, strlen(css));
	assert(n >= 8);

	lam_ui_div_t *root = lam_ui_new(NULL, NULL);
	lam_ui_set_tag(root, "body");

	lam_ui_div_t *b1 = lam_ui_new(root, NULL);
	lam_ui_set_tag(b1, "button");
	lam_ui_set_id(b1, "go");
	lam_ui_class(b1, "btn forced mr-[8px]");

	lam_ui_div_t *b2 = lam_ui_new(root, NULL);
	lam_ui_set_tag(b2, "div");
	lam_ui_class(b2, "mix");

	lam_ui_apply_styles(root, ss);

	/* #go beats .btn and button */
	const lam_ui_style_t *s1 = lam_ui_get_style(b1);
	assert(s1->background_color == LAM_RGB(0x22, 0xaa, 0x44));
	/* .forced !important beats #go.forced */
	assert(s1->color == LAM_RGB(0xff, 0x00, 0x00));
	assert(s1->padding_top == 4 && s1->padding_left == 8);
	assert(s1->margin_right == 8);

	/* color-mix(in srgb, #2c2c2c, transparent 50%) => ~50% alpha, rgb(0x2c,0x2c,0x2c) */
	const lam_ui_style_t *s2 = lam_ui_get_style(b2);
	assert(LAM_COLOR_A(s2->background_color) >= 126 && LAM_COLOR_A(s2->background_color) <= 130);
	assert(LAM_COLOR_R(s2->background_color) == 0x2c);

	/* Toggle :hover on a .btn (without #id overriding) and back off */
	lam_ui_div_t *b3 = lam_ui_new(root, NULL);
	lam_ui_set_tag(b3, "button");
	lam_ui_class(b3, "btn");
	lam_ui_apply_styles(root, ss);
	assert(lam_ui_get_style(b3)->background_color == LAM_RGB(0x33, 0x66, 0x99));

	lam_ui_set_pseudo(b3, LAM_PSEUDO_HOVER);
	lam_ui_apply_styles(root, ss);
	assert(lam_ui_get_style(b3)->background_color == LAM_RGB(0xff, 0x88, 0x00));

	lam_ui_set_pseudo(b3, 0);
	lam_ui_apply_styles(root, ss);
	assert(lam_ui_get_style(b3)->background_color == LAM_RGB(0x33, 0x66, 0x99));

	/* Inline style="..." overrides stylesheet */
	lam_ui_set_inline_style(b3, "width: calc(100vh - 64px); height: 40px;");
	lam_ui_apply_styles(root, ss);
	lam_ui_layout(root);
	assert(lam_ui_get_width(b3) == 720 - 64);
	assert(lam_ui_get_height(b3) == 40);

	lam_ui_destroy(root);
	lam_css_sheet_free(ss);
	printf("  test_specificity_important_and_hover: PASS\n");
}

static void test_layout_wrap_grid_transform_and_hit(void)
{
	lam_ui_init(1000, 600);
	uint32_t ss = lam_ui_stylesheet_init();

	const char *css =
		"#wrap { display: flex; flex-direction: row; flex-wrap: wrap; width: 220px; gap: 10px; }\n"
		".chip { width: 100px; height: 30px; }\n"
		"#grid { display: grid; grid-template-columns: repeat(3, 44px); gap: 4px; width: 140px; height: 140px; }\n"
		".cell { height: 44px; }\n"
		"#hdr { position: absolute; left: 50%; top: 12px; width: 200px; height: 32px; transform: translateX(-50%); }\n"
		"#layer { position: absolute; inset: 0; pointer-events: none; }\n"
		"#win-low { position: absolute; left: 50px; top: 50px; width: 100px; height: 100px; z-index: 10; pointer-events: auto; }\n"
		"#win-hi  { position: absolute; left: 80px; top: 80px; width: 100px; height: 100px; z-index: 20; pointer-events: auto; }\n"
		"#clipbox { position: absolute; left: 300px; top: 50px; width: 80px; height: 80px; overflow: hidden; pointer-events: auto; }\n"
		"#clipchild { position: absolute; left: 0px; top: 100px; width: 60px; height: 40px; pointer-events: auto; }\n";

	assert(lam_css_parse(ss, css, strlen(css)) >= 9);

	lam_ui_div_t *root = lam_ui_new(NULL, NULL);
	lam_ui_set_tag(root, "div");

	/* Flex wrap: 3 chips of 100px inside 220px with 10px gap -> 2 on row 1, 1 on row 2 */
	lam_ui_div_t *wrap = lam_ui_new(root, NULL);
	lam_ui_set_id(wrap, "wrap");
	lam_ui_div_t *c1 = lam_ui_new(wrap, NULL); lam_ui_class(c1, "chip");
	lam_ui_div_t *c2 = lam_ui_new(wrap, NULL); lam_ui_class(c2, "chip");
	lam_ui_div_t *c3 = lam_ui_new(wrap, NULL); lam_ui_class(c3, "chip");

	/* Grid: 3x2 cells of 44px with 4px gap */
	lam_ui_div_t *grid = lam_ui_new(root, NULL);
	lam_ui_set_id(grid, "grid");
	lam_ui_div_t *g[4];
	for (int i = 0; i < 4; i++) {
		g[i] = lam_ui_new(grid, NULL);
		lam_ui_class(g[i], "cell");
	}

	/* Centered header via left:50% + translateX(-50%) */
	lam_ui_div_t *hdr = lam_ui_new(root, NULL);
	lam_ui_set_id(hdr, "hdr");

	/* Pointer-events: none overlay with overlapping z-index children */
	lam_ui_div_t *layer = lam_ui_new(root, NULL);
	lam_ui_set_id(layer, "layer");
	lam_ui_div_t *w_hi  = lam_ui_new(layer, NULL); lam_ui_set_id(w_hi, "win-hi");
	lam_ui_div_t *w_low = lam_ui_new(layer, NULL); lam_ui_set_id(w_low, "win-low");

	/* Overflow: hidden clipping */
	lam_ui_div_t *clipbox = lam_ui_new(root, NULL); lam_ui_set_id(clipbox, "clipbox");
	lam_ui_div_t *clipch  = lam_ui_new(clipbox, NULL); lam_ui_set_id(clipch, "clipchild");

	lam_ui_apply_styles(root, ss);
	lam_ui_layout(root);

	/* Verify flex wrap */
	assert(lam_ui_get_x(c1) == 0 && lam_ui_get_y(c1) == 0);
	assert(lam_ui_get_x(c2) == 110 && lam_ui_get_y(c2) == 0);
	assert(lam_ui_get_x(c3) == 0 && lam_ui_get_y(c3) == 40);

	/* Verify grid */
	assert(lam_ui_get_x(g[0]) == 0 && lam_ui_get_x(g[1]) == 48 && lam_ui_get_x(g[2]) == 96);
	assert(lam_ui_get_x(g[3]) == 0 && lam_ui_get_y(g[3]) == lam_ui_get_y(g[0]) + 48);

	/* Verify transform centering: 1000*0.5 - 200*0.5 = 400 */
	assert(lam_ui_get_x(hdr) == 400 && lam_ui_get_y(hdr) == 12);

	/* Verify z-index hit-testing (even though w_hi precedes w_low in DOM!) */
	assert(lam_ui_hit_test(root, 90, 90) == w_hi);
	assert(lam_ui_hit_test(root, 60, 60) == w_low);
	/* Pointer-events: none layer does not intercept empty area -> falls through to root */
	assert(lam_ui_hit_test(root, 900, 500) == root);
	/* Clipped child outside clipbox is NOT hit; scrolling it into view makes it hittable */
	assert(lam_ui_hit_test(root, 310, 160) != clipch);
	lam_ui_set_scroll(clipbox, 0, 80);
	assert(lam_ui_hit_test(root, 310, 80) == clipch);

	lam_ui_destroy(root);
	lam_css_sheet_free(ss);
	printf("  test_layout_wrap_grid_transform_and_hit: PASS\n");
}

static void test_real_neverdark_stylesheets(void)
{
	lam_ui_init(1280, 800);
	lam_ui_set_dark_mode(1);
	uint32_t ss = lam_ui_stylesheet_init();

	int n1 = lam_css_load_file(ss, "../../htdocs/styles.css");
	int n2 = lam_css_load_file(ss, "../../htdocs/nd.css");
	if (n1 < 0 || n2 < 0) {
		printf("  test_real_neverdark_stylesheets: SKIP (htdocs/*.css not found)\n");
		lam_css_sheet_free(ss);
		return;
	}
	assert(n1 > 100 && n2 > 100);

	/* Build a miniature NeverDark DOM tree */
	lam_ui_div_t *body = lam_ui_new(NULL, NULL);
	lam_ui_set_tag(body, "body");

	lam_ui_div_t *main_el = lam_ui_new(body, NULL);
	lam_ui_set_tag(main_el, "div");
	lam_ui_set_id(main_el, "main");

	lam_ui_div_t *holder = lam_ui_new(main_el, NULL);
	lam_ui_set_tag(holder, "div");
	lam_ui_set_id(holder, "holder");
	lam_ui_class(holder, "flex h-full w-full");

	lam_ui_div_t *room_hdr = lam_ui_new(holder, NULL);
	lam_ui_set_tag(room_hdr, "div");
	lam_ui_set_id(room_hdr, "room-header");

	lam_ui_div_t *keypad = lam_ui_new(holder, NULL);
	lam_ui_set_tag(keypad, "div");
	lam_ui_class(keypad, "nd-keypad");

	lam_ui_div_t *item = lam_ui_new(holder, NULL);
	lam_ui_set_tag(item, "nd-item");

	lam_ui_div_t *btn = lam_ui_new(holder, NULL);
	lam_ui_set_tag(btn, "button");
	lam_ui_class(btn, "btn");

	lam_ui_apply_styles(body, ss);
	lam_ui_layout(body);

	const lam_ui_style_t *sh = lam_ui_get_style(holder);
	assert(sh->display == LAM_UI_DISPLAY_FLEX);
	/* #main > * { padding: 0 !important } beats #holder { padding: 8px; gap: 8px } */
	assert(sh->padding_left == 0 && sh->row_gap == 8 && sh->column_gap == 8);

	const lam_ui_style_t *si = lam_ui_get_style(item);
	assert(si->display == LAM_UI_DISPLAY_FLEX);
	assert(si->column_gap == 16);
	assert(si->border_radius_top_left == 8);

	/* Dark mode button gets var(--cd0)=#2c2c2c and var(--cdf)=#c1c3da */
	const lam_ui_style_t *sbtn = lam_ui_get_style(btn);
	assert(sbtn->background_color == LAM_RGB(0x2c, 0x2c, 0x2c));
	assert(sbtn->color == LAM_RGB(0xc1, 0xc3, 0xda));

	/* body:has(#main) specificity (1,0,1) beats body (0,0,1) -> var(--cb)=#f9f9f9 */
	const lam_ui_style_t *sb = lam_ui_get_style(body);
	assert(sb->background_color == LAM_RGB(0xf9, 0xf9, 0xf9));

	lam_ui_destroy(body);
	lam_css_sheet_free(ss);
	printf("  test_real_neverdark_stylesheets: PASS (styles.css=%d rules, nd.css=%d rules)\n",
	       n1, n2);
}

static void test_css_extended_features(void)
{
	lam_ui_init(1280, 720);
	lam_ui_set_dark_mode(1);
	lam_ui_set_asset_root("../../htdocs");
	uint32_t ss = lam_ui_stylesheet_init();

	const char *css =
		"#row { display: flex; width: 300px; height: 50px; }\n"
		".wrap { display: contents; }\n"
		".item { width: 80px; height: 30px; }\n"
		"body:not(:has(#tog:checked)) .drawer { display: none; }\n"
		"body:has(#tog:checked) .drawer { display: block; }\n"
		"label:not(:has(> input[type=\"checkbox\"])) { color: #ff00ff; }\n"
		"#slide { position: absolute; left: 0; top: 0; width: 100px; height: 40px; transform: translateX(calc(100% + 1rem)); }\n"
		"#lgrad { background: linear-gradient(135deg, #2b4c7e, #1a2d4c); }\n"
		"#rgrad { background: radial-gradient(circle, #444444, #222222); }\n"
		"#shadow-p { text-shadow: 1px 2px #000000; }\n"
		"#outlined { border: 2px solid #112233; outline: .5px solid #ffffff; }\n"
		"#glass { backdrop-filter: blur(4px); max-height: min(100%, 30vh); width: max-content; }\n"
		"#room { background-blend-mode: multiply; background-color: #a9a9a9 !important; }\n"
		"@media (orientation:portrait), only screen and (max-width:500px) { #mob { color: #ff2200; } }\n";

	assert(lam_css_parse(ss, css, strlen(css)) >= 12);

	lam_ui_div_t *body = lam_ui_new(NULL, NULL);
	lam_ui_set_tag(body, "body");

	/* Default flex-direction: row + display: contents flattening */
	lam_ui_div_t *row = lam_ui_new(body, NULL); lam_ui_set_id(row, "row");
	lam_ui_div_t *w   = lam_ui_new(row, NULL);  lam_ui_class(w, "wrap");
	lam_ui_div_t *i1  = lam_ui_new(w, NULL);    lam_ui_class(i1, "item");
	lam_ui_div_t *i2  = lam_ui_new(w, NULL);    lam_ui_class(i2, "item");

	/* :checked + :has + :not(:has) */
	lam_ui_div_t *tog = lam_ui_new(body, NULL);
	lam_ui_set_tag(tog, "input");
	lam_ui_set_id(tog, "tog");
	lam_ui_set_attr(tog, "type", "checkbox");
	lam_ui_div_t *drawer = lam_ui_new(body, NULL);
	lam_ui_class(drawer, "drawer");

	/* :has(> input[type="checkbox"]) */
	lam_ui_div_t *lbl_with = lam_ui_new(body, NULL); lam_ui_set_tag(lbl_with, "label");
	lam_ui_div_t *chk = lam_ui_new(lbl_with, NULL);
	lam_ui_set_tag(chk, "input");
	lam_ui_set_attr(chk, "type", "checkbox");
	lam_ui_div_t *lbl_without = lam_ui_new(body, NULL); lam_ui_set_tag(lbl_without, "label");

	/* transform: translateX(calc(100% + 1rem)) */
	lam_ui_div_t *slide = lam_ui_new(body, NULL); lam_ui_set_id(slide, "slide");

	/* gradients */
	lam_ui_div_t *lgrad = lam_ui_new(body, NULL); lam_ui_set_id(lgrad, "lgrad");
	lam_ui_div_t *rgrad = lam_ui_new(body, NULL); lam_ui_set_id(rgrad, "rgrad");

	/* text-shadow inheritance */
	lam_ui_div_t *sp = lam_ui_new(body, NULL); lam_ui_set_id(sp, "shadow-p");
	lam_ui_div_t *sc = lam_ui_new(sp, NULL);

	/* outline separate from border */
	lam_ui_div_t *out = lam_ui_new(body, NULL); lam_ui_set_id(out, "outlined");

	/* backdrop-filter + min() + max-content */
	lam_ui_div_t *glass = lam_ui_new(body, NULL); lam_ui_set_id(glass, "glass");

	/* background url() preserving !important background-color + multiply */
	lam_ui_div_t *room = lam_ui_new(body, NULL);
	lam_ui_set_id(room, "room");
	lam_ui_set_inline_style(room, "background: url('/nd/art/biome/water/1.jpeg') center/cover no-repeat;");

	/* comma @media */
	lam_ui_div_t *mob = lam_ui_new(body, NULL); lam_ui_set_id(mob, "mob");

	lam_ui_apply_styles(body, ss);
	lam_ui_layout(body);

	/* 1. Default flex-direction: row + display:contents flattens i1,i2 horizontally */
	assert(lam_ui_get_style(row)->flex_direction == LAM_UI_ROW);
	assert(lam_ui_get_style(w)->display == LAM_UI_DISPLAY_CONTENTS);
	assert(lam_ui_get_x(i1) == 0 && lam_ui_get_y(i1) == 0);
	assert(lam_ui_get_x(i2) == 80 && lam_ui_get_y(i2) == 0);
	assert(lam_ui_hit_test(body, 90, 10) == i2);

	/* 2. :checked + recursive :has */
	assert(lam_ui_get_style(drawer)->display == LAM_UI_DISPLAY_NONE);
	assert(lam_ui_get_style(lbl_with)->color != LAM_RGB(0xff, 0x00, 0xff));
	assert(lam_ui_get_style(lbl_without)->color == LAM_RGB(0xff, 0x00, 0xff));
	lam_ui_set_pseudo(tog, LAM_PSEUDO_CHECKED);
	lam_ui_apply_styles(body, ss);
	assert(lam_ui_get_style(drawer)->display == LAM_UI_DISPLAY_BLOCK);

	/* 3. translateX(calc(100% + 1rem)) => 0 + 100 + 16 = 116 */
	assert(lam_ui_get_x(slide) == 116);

	/* 4. Gradients */
	const lam_ui_style_t *slg = lam_ui_get_style(lgrad);
	assert(slg->bg_grad_type == 1 && slg->bg_grad_angle > 134.9f && slg->bg_grad_angle < 135.1f);
	assert(slg->background_color == LAM_RGB(0x2b, 0x4c, 0x7e));
	assert(slg->background_color2 == LAM_RGB(0x1a, 0x2d, 0x4c));
	const lam_ui_style_t *srg = lam_ui_get_style(rgrad);
	assert(srg->bg_grad_type == 2);
	assert(srg->background_color == LAM_RGB(0x44, 0x44, 0x44));
	assert(srg->background_color2 == LAM_RGB(0x22, 0x22, 0x22));

	/* 5. text-shadow inheritance */
	const lam_ui_style_t *ssc = lam_ui_get_style(sc);
	assert(ssc->text_shadow_x == 1 && ssc->text_shadow_y == 2);
	assert(ssc->text_shadow_color == LAM_RGB(0x00, 0x00, 0x00));

	/* 6. outline separate from border */
	const lam_ui_style_t *so = lam_ui_get_style(out);
	assert(so->border_width == 2 && so->border_color == LAM_RGB(0x11, 0x22, 0x33));
	assert(so->outline_width == 1 && so->outline_color == LAM_RGB(0xff, 0xff, 0xff));

	/* 7. backdrop-filter + min(100%, 30vh) */
	const lam_ui_style_t *sgl = lam_ui_get_style(glass);
	assert(sgl->backdrop_blur > 3.9f && sgl->backdrop_blur < 4.1f);
	assert(sgl->max_height.unit == LAM_LEN_VH && sgl->max_height.val > 29.9f);
	assert(sgl->width_len.unit == LAM_LEN_AUTO);

	/* 8. background url() preserves !important #a9a9a9 + multiply + cover */
	const lam_ui_style_t *srm = lam_ui_get_style(room);
	assert(srm->background_color == LAM_RGB(0xa9, 0xa9, 0xa9));
	assert(srm->bg_blend_mode == 1);
	assert(srm->bg_size_mode == 1);

	/* 9. comma-separated @media OR semantics */
	assert(lam_ui_get_style(mob)->color != LAM_RGB(0xff, 0x22, 0x00)); /* 1280x720 desktop */
	lam_ui_resize(400, 300); /* landscape narrow -> max-width:500px matches */
	lam_ui_apply_styles(body, ss);
	assert(lam_ui_get_style(mob)->color == LAM_RGB(0xff, 0x22, 0x00));
	lam_ui_resize(600, 800); /* portrait wide -> orientation:portrait matches */
	lam_ui_apply_styles(body, ss);
	assert(lam_ui_get_style(mob)->color == LAM_RGB(0xff, 0x22, 0x00));
	lam_ui_resize(1280, 720);

	lam_ui_destroy(body);
	lam_css_sheet_free(ss);
	printf("  test_css_extended_features: PASS\n");
}

static void test_indefinite_percent_sizing(void)
{
	const char *css =
		"#root { position: relative; width: 1280px; height: 720px; }"
		"#dock { position: absolute; right: 16px; bottom: 16px; width: auto; height: auto; padding: 6px 10px; }"
		".inner { width: 100%; height: 100%; display: flex; flex-direction: row; gap: 6px; }"
		".btn { width: 36px; height: 36px; }"
		"#bar { width: 240px; height: auto; outline: 1px solid #ffffff; }"
		".fill { width: 100%; height: 100%; }";

	lam_ui_resize(1280, 720);
	uint32_t ss = lam_ui_stylesheet_init();
	assert(lam_css_parse(ss, css, strlen(css)) > 0);

	lam_ui_div_t *root = lam_ui_new(NULL, NULL);
	lam_ui_set_id(root, "root");

	lam_ui_div_t *dock = lam_ui_new(root, NULL);
	lam_ui_set_id(dock, "dock");
	lam_ui_div_t *inner = lam_ui_new(dock, NULL);
	lam_ui_class(inner, "inner");
	for (int i = 0; i < 3; i++) {
		lam_ui_div_t *b = lam_ui_new(inner, NULL);
		lam_ui_class(b, "btn");
	}

	lam_ui_div_t *bar = lam_ui_new(root, NULL);
	lam_ui_set_id(bar, "bar");
	lam_ui_div_t *fill = lam_ui_new(bar, NULL);
	lam_ui_class(fill, "fill");

	lam_ui_apply_styles(root, ss);
	lam_ui_layout(root);

	/* 3*36 + 2*6 = 120 content + 20 horiz padding = 140w; 36 + 12 = 48h */
	assert(lam_ui_get_width(dock) == 140);
	assert(lam_ui_get_height(dock) == 48);
	assert(lam_ui_get_x(dock) == 1280 - 16 - 140);
	assert(lam_ui_get_y(dock) == 720 - 16 - 48);

	/* height: 100% inside height: auto resolves to 0, not 720 */
	assert(lam_ui_get_width(bar) == 240);
	assert(lam_ui_get_height(bar) == 0);

	lam_ui_destroy(root);
	lam_css_sheet_free(ss);
	printf("  test_indefinite_percent_sizing: PASS\n");
}

static void test_css_cursor_and_hover_handles(void)
{
	const char *css =
		"#root { position: relative; width: 800px; height: 600px; }"
		".win { position: absolute; left: 100px; top: 100px; width: 300px; height: 200px; overflow: visible; }"
		".tb { position: absolute; top: -24px; left: 0; right: 0; height: 24px; opacity: 0; pointer-events: none; cursor: move !important; }"
		".win:hover > .tb { opacity: 1 !important; pointer-events: auto !important; }"
		".rs { position: absolute; right: 0; top: 10px; bottom: 10px; width: 6px; pointer-events: none; cursor: ew-resize !important; }"
		".win:hover > .rs { pointer-events: auto !important; }"
		"button { cursor: pointer; }"
		"button * { pointer-events: none; }"
		".canvas { cursor: grab; }";

	lam_ui_resize(800, 600);
	uint32_t ss = lam_ui_stylesheet_init();
	assert(lam_css_parse(ss, css, strlen(css)) > 0);

	lam_ui_div_t *root = lam_ui_new(NULL, NULL);
	lam_ui_set_id(root, "root");
	lam_ui_div_t *win = lam_ui_new(root, NULL);
	lam_ui_class(win, "win");
	lam_ui_div_t *tb = lam_ui_new(win, NULL);
	lam_ui_class(tb, "tb");
	lam_ui_div_t *tb_title = lam_ui_new(tb, NULL); /* inherits cursor: move */
	lam_ui_div_t *rs = lam_ui_new(win, NULL);
	lam_ui_class(rs, "rs");
	lam_ui_div_t *btn = lam_ui_new(win, NULL);
	lam_ui_set_tag(btn, "button");
	lam_ui_set_inline_style(btn, "position:absolute; left:10px; top:10px; width:80px; height:30px;");
	lam_ui_div_t *icon = lam_ui_new(btn, NULL);
	lam_ui_set_tag(icon, "span");
	lam_ui_set_inline_style(icon, "width:20px; height:20px;");
	lam_ui_div_t *cvs = lam_ui_new(root, NULL);
	lam_ui_class(cvs, "canvas");

	lam_ui_apply_styles(root, ss);
	lam_ui_layout(root);

	/* Cursor parsing & inheritance */
	assert(lam_ui_get_style(tb)->cursor == LAM_CURSOR_MOVE);
	assert(lam_ui_get_style(tb_title)->cursor == LAM_CURSOR_MOVE);
	assert(lam_ui_get_style(rs)->cursor == LAM_CURSOR_EW_RESIZE);
	assert(lam_ui_get_style(btn)->cursor == LAM_CURSOR_POINTER);
	assert(lam_ui_get_style(icon)->cursor == LAM_CURSOR_POINTER);
	assert(lam_ui_get_style(icon)->pointer_events == LAM_UI_PE_NONE);
	assert(lam_ui_get_style(cvs)->cursor == LAM_CURSOR_GRAB);

	/* Clicking icon inside button hits button because of button * { pointer-events: none } */
	assert(lam_ui_hit_test(root, 115, 115) == btn);

	/* Toolbar at top:-24px is non-hittable until .win has :hover */
	assert(lam_ui_hit_test(root, 150, 88) == root);
	lam_ui_set_pseudo(win, LAM_PSEUDO_HOVER);
	lam_ui_apply_styles(root, ss);
	assert(lam_ui_get_style(tb)->pointer_events == LAM_UI_PE_AUTO);
	assert(lam_ui_get_style(rs)->pointer_events == LAM_UI_PE_AUTO);
	assert(lam_ui_hit_test(root, 150, 88) == tb);
	assert(lam_ui_hit_test(root, 397, 150) == rs);

	lam_ui_destroy(root);
	lam_css_sheet_free(ss);
	printf("  test_css_cursor_and_hover_handles: PASS\n");
}

int main(void)
{
	printf("test_css_parser:\n");
	lam_init();

	test_specificity_important_and_hover();
	test_layout_wrap_grid_transform_and_hit();
	test_real_neverdark_stylesheets();
	test_css_extended_features();
	test_indefinite_percent_sizing();
	test_css_cursor_and_hover_handles();

	printf("test_css_parser: ALL TESTS PASSED\n");
	return 0;
}
