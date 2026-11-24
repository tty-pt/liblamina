#include <ttypt/lam-ui.h>
#include <ttypt/lam-bud.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Part 1: Unit test liblamina tree mutation primitives ─────────────── */

static void test_lam_ui_tree_mutations(void)
{
	lam_ui_init(800, 600);
	lam_ui_div_t *p = lam_ui_new(NULL, NULL);
	lam_ui_div_t *a = lam_ui_new(NULL, NULL);
	lam_ui_div_t *b = lam_ui_new(NULL, NULL);
	lam_ui_div_t *c = lam_ui_new(NULL, NULL);

	lam_ui_set_id(a, "a");
	lam_ui_set_id(b, "b");
	lam_ui_set_id(c, "c");

	/* Append via insert_before(..., NULL) */
	lam_ui_insert_before(p, a, NULL);
	lam_ui_insert_before(p, c, NULL);
	/* Insert b before c -> [a, b, c] */
	lam_ui_insert_before(p, b, c);

	assert(lam_ui_first_child(p) == a);
	assert(lam_ui_next_sibling(a) == b);
	assert(lam_ui_next_sibling(b) == c);
	assert(lam_ui_next_sibling(c) == NULL);

	/* Move c before a -> [c, a, b] */
	lam_ui_insert_before(p, c, a);
	assert(lam_ui_first_child(p) == c);
	assert(lam_ui_next_sibling(c) == a);
	assert(lam_ui_next_sibling(a) == b);
	assert(lam_ui_next_sibling(b) == NULL);

	/* Remove middle child a -> [c, b] */
	lam_ui_remove_child(p, a);
	assert(lam_ui_parent(a) == NULL);
	assert(lam_ui_first_child(p) == c);
	assert(lam_ui_next_sibling(c) == b);
	assert(lam_ui_next_sibling(b) == NULL);

	/* Attr set + remove */
	lam_ui_set_attr(a, "data-role", "item");
	lam_ui_set_attr(a, "data-id", "7");
	assert(strcmp(lam_ui_get_attr(a, "data-role"), "item") == 0);
	lam_ui_remove_attr(a, "data-role");
	assert(lam_ui_get_attr(a, "data-role") == NULL);
	assert(strcmp(lam_ui_get_attr(a, "data-id"), "7") == 0);

	lam_ui_destroy(a);
	lam_ui_destroy(p);
	printf("  test_lam_ui_tree_mutations: PASS\n");
}

/* ── Part 2: Isomorphic reactive C components (SSR + Native Lamina) ───── */

typedef struct {
	const char *label;
	const char *id;
} like_btn_props_t;

static int on_like(bud_event *ev)
{
	int *count = (int *)ev->user;
	bud_set(count, *count + 1);
	return 0;
}

static bud_node *LikeButton(void *ctx, const void *raw_props)
{
	(void)ctx;
	const like_btn_props_t *p = (const like_btn_props_t *)raw_props;
	int *likes = bud_use_state(int, 0);

	bud_node *btn = bud_element("button");
	bud_set_attr(btn, "id", p->id);
	bud_set_attr(btn, "class", "btn");
	bud_bind_ctx(btn, "click", 1, on_like, likes, 0);
	bud_append(btn, bud_textf("%s:%d", p->label, *likes));
	return btn;
}

typedef struct {
	const char *const *songs;
	size_t count;
	int *deleted_idx_out;
} app_props_t;

typedef struct {
	int *deleted_idx_out;
	int  idx;
} del_ctx_t;

static int on_del_song(bud_event *ev)
{
	del_ctx_t *d = (del_ctx_t *)ev->user;
	*d->deleted_idx_out = d->idx;
	return 0;
}

static bud_node *MusicPanel(void *ctx, const void *raw_props)
{
	(void)ctx;
	const app_props_t *p = (const app_props_t *)raw_props;

	bud_node *panel = bud_element("div");
	bud_set_attr(panel, "class", "panel");

	/* Toolbar with two independent stateful LikeButtons */
	bud_node *bar = bud_element("div");
	bud_set_attr(bar, "class", "toolbar");
	bud_append(bar, bud_comp(LikeButton, like_btn_props_t,
	                         .label = "Band", .id = "like-band"));
	bud_append(bar, bud_comp(LikeButton, like_btn_props_t,
	                         .label = "Encore", .id = "like-encore"));
	bud_append(panel, bar);

	/* Keyed song list */
	bud_node *list = bud_element("ul");
	bud_set_attr(list, "class", "songs");
	for (size_t i = 0; i < p->count; i++) {
		bud_node *li = bud_element("li");
		bud_set_key(li, p->songs[i]);
		bud_set_attr(li, "class", "song-row");
		del_ctx_t dctx = { .deleted_idx_out = p->deleted_idx_out, .idx = (int)i };
		bud_bind_ctx(li, "click", 1, on_del_song, &dctx, sizeof(dctx));
		bud_append(li, bud_child_any(p->songs[i]));
		bud_append(list, li);
	}
	bud_append(panel, list);

	return panel;
}

static void test_bud_lamina_css_layout_hit_test_and_ssr(void)
{
	lam_ui_init(800, 600);
	uint32_t ss = lam_ui_stylesheet_init();
	const char *css =
		".panel { display: flex; flex-direction: column; width: 400px; height: 300px; padding: 10px; }\n"
		".toolbar { display: flex; flex-direction: row; width: 380px; height: 40px; gap: 10px; }\n"
		".btn { width: 120px; height: 36px; }\n"
		".songs { display: flex; flex-direction: column; width: 380px; gap: 4px; }\n"
		".song-row { width: 380px; height: 28px; }\n";
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
	bud_node *vtree1 = bud_comp(MusicPanel, app_props_t,
	                            .songs = songs1, .count = 3,
	                            .deleted_idx_out = &deleted_idx);

	/* 1. Prove exact same component renders valid SSR HTML! */
	char *html = bud_render_html(vtree1);
	assert(html != NULL);
	assert(strstr(html, "<button id=\"like-band\" class=\"btn\">Band:0</button>") != NULL);
	assert(strstr(html, "<button id=\"like-encore\" class=\"btn\">Encore:0</button>") != NULL);
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
	lam_ui_div_t *list    = lam_ui_next_sibling(toolbar);
	lam_ui_div_t *row0    = lam_ui_first_child(list);
	lam_ui_div_t *row1    = lam_ui_next_sibling(row0);
	lam_ui_div_t *row2    = lam_ui_next_sibling(row1);

	assert(panel && toolbar && btn_a && btn_b && list);
	assert(row0 && row1 && row2);
	assert(strcmp(lam_ui_get_id(btn_a), "like-band") == 0);
	assert(strcmp(lam_ui_get_id(btn_b), "like-encore") == 0);
	assert(lam_ui_get_width(btn_a) == 120 && lam_ui_get_height(btn_a) == 36);
	assert(lam_ui_get_x(btn_b) > lam_ui_get_x(btn_a));

	/* 3. Click LikeButton A via real coordinate hit-test -> only A increments! */
	int ax = lam_ui_get_x(btn_a) + 10;
	int ay = lam_ui_get_y(btn_a) + 10;
	assert(lam_bud_dispatch_click(root, ax, ay) == 1);
	assert(bud_mount_is_dirty(mount) == 1);
	assert(bud_mount_flush(mount) == 1);
	lam_bud_sync_layout(&host, root);

	assert(strcmp(lam_ui_get_text(lam_ui_first_child(btn_a)), "Band:1") == 0);
	assert(strcmp(lam_ui_get_text(lam_ui_first_child(btn_b)), "Encore:0") == 0);

	/* 4. Keyed reorder: swap Alpha <-> Gamma and verify EXACT lam_ui_div_t pointer identity! */
	const char *songs2[3] = { "Gamma", "Beta", "Alpha" };
	bud_node *vtree2 = bud_comp(MusicPanel, app_props_t,
	                            .songs = songs2, .count = 3,
	                            .deleted_idx_out = &deleted_idx);
	assert(bud_mount_update(mount, vtree2) == 0);
	bud_free(vtree2);
	lam_bud_sync_layout(&host, root);

	/* Reordered children MUST reuse exact original lam_ui_div_t pointers! */
	lam_ui_div_t *n0 = lam_ui_first_child(list);
	lam_ui_div_t *n1 = lam_ui_next_sibling(n0);
	lam_ui_div_t *n2 = lam_ui_next_sibling(n1);
	assert(n0 == row2); /* Gamma moved to index 0 */
	assert(n1 == row1); /* Beta stayed at index 1 */
	assert(n2 == row0); /* Alpha moved to index 2 */

	/* Local hook state on LikeButton A MUST survive parent update! */
	assert(strcmp(lam_ui_get_text(lam_ui_first_child(btn_a)), "Band:1") == 0);

	/* 5. Click reordered row 0 ("Gamma") via hit-test -> fires updated inline closure idx 0 */
	int rx = lam_ui_get_x(n0) + 10;
	int ry = lam_ui_get_y(n0) + 10;
	assert(lam_bud_dispatch_click(root, rx, ry) == 1);
	assert(deleted_idx == 0);

	/* 6. Clean unmount & destroy */
	bud_mount_free(mount);
	assert(lam_ui_first_child(root) == NULL);
	lam_ui_destroy(root);
	lam_css_sheet_free(ss);
	printf("  test_bud_lamina_css_layout_hit_test_and_ssr: PASS\n");
}

int main(void)
{
	printf("test_bud_lamina:\n");
	test_lam_ui_tree_mutations();
	test_bud_lamina_css_layout_hit_test_and_ssr();
	printf("test_bud_lamina: ALL TESTS PASSED\n");
	return 0;
}
