/**
 * @file test_cx_lamina.c
 * @brief End-to-end test: `.cx` -> `cx` transpiler -> C11 -> SSR + native liblamina CSS/hit-test.
 */

#include <ttypt/lam-ui.h>
#include <ttypt/lam-bud.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Transpiled from music_panel.cx by external/libbud/bin/cx */
#include "music_panel.cx.c"

int main(void)
{
	printf("test_cx_lamina:\n");

	lam_ui_init(800, 600);
	uint32_t ss = lam_ui_stylesheet_init();
	const char *css =
		".panel { display: flex; flex-direction: column; width: 420px; height: 320px; padding: 10px; }\n"
		".toolbar { display: flex; flex-direction: row; width: 400px; height: 40px; gap: 10px; }\n"
		".btn { width: 110px; height: 36px; }\n"
		".badge { width: 120px; height: 36px; }\n"
		".songs { display: flex; flex-direction: column; width: 400px; gap: 4px; }\n"
		".song-row { width: 400px; height: 28px; }\n";
	assert(lam_css_parse(ss, css, strlen(css)) > 0);

	lam_ui_div_t *root = lam_ui_new(NULL, NULL);
	lam_ui_set_size(root, 800, 600);

	lam_bud_host_t host;
	lam_bud_host_init(&host, ss);
	bud_host_ops ops = lam_bud_host_ops();
	bud_mount *mount = bud_mount_new(&ops, &host, root);
	assert(mount != NULL);

	const char *songs1[3] = { "Alpha", "Beta", "Gamma" };
	int deleted_idx = -1;

	/* 1. Render SSR HTML from the exact .cx-generated MusicPanel_render */
	bud_node *vtree1 = bud_comp(MusicPanel_render, MusicPanel_props,
	                            .songs = songs1, .count = 3,
	                            .deleted_idx_out = &deleted_idx);
	char *html = bud_render_html(vtree1);
	assert(html != NULL);
	assert(strstr(html, "<button id=\"like-band\" class=\"btn\" data-likes=\"0\">Band:0</button>") != NULL);
	assert(strstr(html, "<button id=\"like-encore\" class=\"btn\" data-likes=\"0\">Encore:0</button>") != NULL);
	assert(strstr(html, "<span class=\"badge\" data-count=\"3\">Tracks (3)</span>") != NULL);
	assert(strstr(html, "<li data-bud-key=\"Gamma\" class=\"song-row\">Gamma</li>") != NULL);
	free(html);

	/* 2. Mount into native liblamina CSS UI tree and compute layout */
	assert(bud_mount_update(mount, vtree1) == 0);
	bud_free(vtree1);
	lam_bud_sync_layout(&host, root);

	lam_ui_div_t *panel   = lam_ui_first_child(root);
	lam_ui_div_t *toolbar = lam_ui_first_child(panel);
	lam_ui_div_t *btn_a   = lam_ui_first_child(toolbar);
	lam_ui_div_t *btn_b   = lam_ui_next_sibling(btn_a);
	lam_ui_div_t *badge   = lam_ui_next_sibling(btn_b);
	lam_ui_div_t *list    = lam_ui_next_sibling(toolbar);
	lam_ui_div_t *row0    = lam_ui_first_child(list);
	lam_ui_div_t *row1    = lam_ui_next_sibling(row0);
	lam_ui_div_t *row2    = lam_ui_next_sibling(row1);

	assert(panel && toolbar && btn_a && btn_b && badge && list);
	assert(row0 && row1 && row2);
	assert(strcmp(lam_ui_get_id(btn_a), "like-band") == 0);
	assert(strcmp(lam_ui_get_id(btn_b), "like-encore") == 0);
	assert(strcmp(lam_ui_get_attr(badge, "data-count"), "3") == 0);
	assert(lam_ui_get_x(btn_b) > lam_ui_get_x(btn_a));

	/* 3. Click LikeButton A via hit-test -> lifted C99 closure mutates only A! */
	int ax = lam_ui_get_x(btn_a) + 10;
	int ay = lam_ui_get_y(btn_a) + 10;
	assert(lam_bud_dispatch_click(root, ax, ay) == 1);
	assert(bud_mount_is_dirty(mount) == 1);
	assert(bud_mount_flush(mount) == 1);
	lam_bud_sync_layout(&host, root);

	assert(strcmp(lam_ui_get_attr(btn_a, "data-likes"), "1") == 0);
	assert(strcmp(lam_ui_get_attr(btn_b, "data-likes"), "0") == 0);

	/* 4. Keyed reorder: Gamma <-> Alpha -> preserves exact lam_ui_div_t pointers! */
	const char *songs2[3] = { "Gamma", "Beta", "Alpha" };
	bud_node *vtree2 = bud_comp(MusicPanel_render, MusicPanel_props,
	                            .songs = songs2, .count = 3,
	                            .deleted_idx_out = &deleted_idx);
	assert(bud_mount_update(mount, vtree2) == 0);
	bud_free(vtree2);
	lam_bud_sync_layout(&host, root);

	lam_ui_div_t *n0 = lam_ui_first_child(list);
	lam_ui_div_t *n1 = lam_ui_next_sibling(n0);
	lam_ui_div_t *n2 = lam_ui_next_sibling(n1);
	assert(n0 == row2);
	assert(n1 == row1);
	assert(n2 == row0);

	/* 5. Click reordered row 0 ("Gamma") -> lifted shorthand+init closure fires idx=0! */
	int rx = lam_ui_get_x(n0) + 10;
	int ry = lam_ui_get_y(n0) + 10;
	assert(lam_bud_dispatch_click(root, rx, ry) == 1);
	assert(deleted_idx == 0);

	/* 6. Click row 2 ("Alpha") -> fires idx=2! */
	int r2x = lam_ui_get_x(n2) + 10;
	int r2y = lam_ui_get_y(n2) + 10;
	assert(lam_bud_dispatch_click(root, r2x, r2y) == 1);
	assert(deleted_idx == 2);

	/* 7. Empty list conditional `{if (count == 0) <li class="empty">No songs</li>}` */
	bud_node *vtree3 = bud_comp(MusicPanel_render, MusicPanel_props,
	                            .songs = NULL, .count = 0,
	                            .deleted_idx_out = &deleted_idx);
	char *empty_html = bud_render_html(vtree3);
	assert(strstr(empty_html, "<li class=\"empty\">No songs</li>") != NULL);
	free(empty_html);
	assert(bud_mount_update(mount, vtree3) == 0);
	bud_free(vtree3);

	bud_mount_free(mount);
	lam_ui_destroy(root);
	lam_css_sheet_free(ss);

	printf("  test_cx_music_panel_ssr_and_lamina: PASS\n");
	printf("test_cx_lamina: ALL TESTS PASSED\n");
	return 0;
}
