#ifndef LAM_BUD_H
#define LAM_BUD_H

/**
 * @file lam-bud.h
 * @brief Zero-link-dependency adapter bridging libbud's bud_host_ops reconciler
 *        onto liblamina's native/WebGL CSS UI engine (lam-ui.h).
 *
 * Neither libbud nor liblamina depends on the other at link time; consumers
 * linking both `-lbud -llamina` include this header to mount reactive Bud
 * component trees directly into a lam_ui_div_t root.
 */

#include <ttypt/lam-ui.h>
#include <bud/bud_reconcile.h>

#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lam_bud_ev {
	char                *event;
	int                  bubbles;
	bud_event_handler_fn fn;
	void                *ud;
	struct lam_bud_ev   *next;
} lam_bud_ev_t;

typedef struct lam_bud_host {
	uint32_t stylesheet;   /* optional stylesheet handle (0 if none) */
	int      style_dirty;  /* set to 1 whenever tree/attrs/text change */
} lam_bud_host_t;

static inline char *lam_bud_strdup(const char *s)
{
	if (!s) return NULL;
	size_t n = strlen(s);
	char *r = (char *)malloc(n + 1);
	if (r) memcpy(r, s, n + 1);
	return r;
}

static inline void lam_bud_free_node_events(lam_ui_div_t *d)
{
	if (!d) return;
	lam_bud_ev_t *ev = (lam_bud_ev_t *)lam_ui_get_user_data(d);
	while (ev) {
		lam_bud_ev_t *nx = ev->next;
		free(ev->event);
		free(ev);
		ev = nx;
	}
	lam_ui_set_user_data(d, NULL);
}

static inline void lam_bud_free_tree_events(lam_ui_div_t *d)
{
	if (!d) return;
	for (lam_ui_div_t *c = lam_ui_first_child(d); c; c = lam_ui_next_sibling(c))
		lam_bud_free_tree_events(c);
	lam_bud_free_node_events(d);
}

static inline void *lam_bud_op_create_el(void *ctx, const char *tag)
{
	lam_bud_host_t *h = (lam_bud_host_t *)ctx;
	lam_ui_div_t *d = lam_ui_new(NULL, NULL);
	if (d && tag)
		lam_ui_set_tag(d, tag);
	if (h)
		h->style_dirty = 1;
	return d;
}

static inline void *lam_bud_op_create_text(void *ctx, const char *text, size_t len)
{
	(void)len;
	lam_bud_host_t *h = (lam_bud_host_t *)ctx;
	lam_ui_div_t *d = lam_ui_new(NULL, NULL);
	if (d) {
		lam_ui_set_tag(d, "#text");
		lam_ui_text(d, text ? text : "");
	}
	if (h)
		h->style_dirty = 1;
	return d;
}

static inline void lam_bud_op_set_text(void *ctx, void *hn, const char *text, size_t len)
{
	(void)len;
	lam_bud_host_t *h = (lam_bud_host_t *)ctx;
	lam_ui_text((lam_ui_div_t *)hn, text ? text : "");
	if (h)
		h->style_dirty = 1;
}

static inline void lam_bud_op_set_attr(void *ctx, void *hn, const char *name, const char *val)
{
	lam_bud_host_t *h = (lam_bud_host_t *)ctx;
	lam_ui_div_t *d = (lam_ui_div_t *)hn;
	if (!d || !name) return;
	if (strcmp(name, "class") == 0)
		lam_ui_class(d, val);
	else if (strcmp(name, "id") == 0)
		lam_ui_set_id(d, val);
	else if (strcmp(name, "style") == 0)
		lam_ui_set_inline_style(d, val);
	else
		lam_ui_set_attr(d, name, val);
	if (h)
		h->style_dirty = 1;
}

static inline void lam_bud_op_remove_attr(void *ctx, void *hn, const char *name)
{
	lam_bud_host_t *h = (lam_bud_host_t *)ctx;
	lam_ui_div_t *d = (lam_ui_div_t *)hn;
	if (!d || !name) return;
	if (strcmp(name, "class") == 0)
		lam_ui_class(d, NULL);
	else if (strcmp(name, "id") == 0)
		lam_ui_set_id(d, NULL);
	else if (strcmp(name, "style") == 0)
		lam_ui_set_inline_style(d, NULL);
	else
		lam_ui_remove_attr(d, name);
	if (h)
		h->style_dirty = 1;
}

static inline void lam_bud_op_insert_before(void *ctx, void *hp, void *hc, void *hb)
{
	lam_bud_host_t *h = (lam_bud_host_t *)ctx;
	lam_ui_insert_before(
	        (lam_ui_div_t *)hp, (lam_ui_div_t *)hc, (lam_ui_div_t *)hb);
	if (h)
		h->style_dirty = 1;
}

static inline void lam_bud_op_remove_child(void *ctx, void *hp, void *hc)
{
	lam_bud_host_t *h = (lam_bud_host_t *)ctx;
	lam_ui_remove_child((lam_ui_div_t *)hp, (lam_ui_div_t *)hc);
	if (h)
		h->style_dirty = 1;
}

static inline void lam_bud_op_destroy_node(void *ctx, void *hn)
{
	lam_bud_host_t *h = (lam_bud_host_t *)ctx;
	lam_ui_div_t *d = (lam_ui_div_t *)hn;
	if (!d) return;
	lam_bud_free_tree_events(d);
	lam_ui_destroy(d);
	if (h)
		h->style_dirty = 1;
}

static inline void lam_bud_op_clear_children(void *ctx, void *hp)
{
	lam_bud_host_t *h = (lam_bud_host_t *)ctx;
	lam_ui_div_t *p = (lam_ui_div_t *)hp;
	if (!p) return;
	for (lam_ui_div_t *c = lam_ui_first_child(p); c; c = lam_ui_next_sibling(c))
		lam_bud_free_tree_events(c);
	lam_ui_clear(p);
	if (h)
		h->style_dirty = 1;
}

static inline void lam_bud_op_set_event(
        void *ctx, void *hn, const char *event, int bubbles,
        bud_event_handler_fn fn, void *ud)
{
	(void)ctx;
	lam_ui_div_t *d = (lam_ui_div_t *)hn;
	if (!d || !event) return;

	lam_bud_ev_t *head = (lam_bud_ev_t *)lam_ui_get_user_data(d);
	lam_bud_ev_t **pp = &head;
	while (*pp) {
		if (strcmp((*pp)->event, event) == 0) {
			if (!fn) {
				lam_bud_ev_t *rm = *pp;
				*pp = rm->next;
				free(rm->event);
				free(rm);
				lam_ui_set_user_data(d, head);
			} else {
				(*pp)->bubbles = bubbles;
				(*pp)->fn = fn;
				(*pp)->ud = ud;
			}
			return;
		}
		pp = &(*pp)->next;
	}

	if (fn) {
		lam_bud_ev_t *ev = (lam_bud_ev_t *)calloc(1, sizeof(*ev));
		if (!ev) return;
		ev->event = lam_bud_strdup(event);
		ev->bubbles = bubbles;
		ev->fn = fn;
		ev->ud = ud;
		ev->next = head;
		lam_ui_set_user_data(d, ev);
	}
}

static inline void lam_bud_host_init(lam_bud_host_t *host, uint32_t stylesheet)
{
	if (!host) return;
	host->stylesheet = stylesheet;
	host->style_dirty = 1;
}

static inline bud_host_ops lam_bud_host_ops(void)
{
	bud_host_ops ops;
	ops.create_el      = lam_bud_op_create_el;
	ops.create_text    = lam_bud_op_create_text;
	ops.set_text       = lam_bud_op_set_text;
	ops.set_attr       = lam_bud_op_set_attr;
	ops.remove_attr    = lam_bud_op_remove_attr;
	ops.insert_before  = lam_bud_op_insert_before;
	ops.remove_child   = lam_bud_op_remove_child;
	ops.destroy_node   = lam_bud_op_destroy_node;
	ops.set_event      = lam_bud_op_set_event;
	ops.clear_children = lam_bud_op_clear_children;
	return ops;
}

/**
 * @brief Re-apply CSS stylesheet and layout when the reconciled tree changed.
 */
static inline void lam_bud_sync_layout(lam_bud_host_t *host, lam_ui_div_t *root)
{
	if (!root) return;
	if (!host || host->style_dirty) {
		if (host && host->stylesheet)
			lam_ui_apply_styles(root, host->stylesheet);
		lam_ui_layout(root);
		if (host)
			host->style_dirty = 0;
	}
}

/**
 * @brief Dispatch an event starting at target_div and bubbling up lam_ui_parent.
 * @return Number of matching handlers invoked.
 */
static inline int lam_bud_dispatch_event(
        lam_ui_div_t *target, const char *event_name, void *payload)
{
	if (!target || !event_name) return 0;

	bud_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = event_name;
	ev.bubbles = 1;

	int fired = 0;
	for (lam_ui_div_t *cur = target; cur; cur = lam_ui_parent(cur)) {
		int is_bubbled = (cur != target);
		lam_bud_ev_t *l = (lam_bud_ev_t *)lam_ui_get_user_data(cur);
		for (; l; l = l->next) {
			if (strcmp(l->event, event_name) != 0)
				continue;
			if (is_bubbled && !l->bubbles)
				continue;
			if (l->fn) {
				ev.user = l->ud ? l->ud : payload;
				l->fn(&ev);
				fired++;
				if (ev.stopped)
					return fired;
			}
		}
		if (!ev.bubbles || ev.stopped)
			break;
	}
	return fired;
}

/**
 * @brief Hit-test (px, py) in root and dispatch a bubbling "click" event.
 * @return Number of matching click handlers invoked.
 */
static inline int lam_bud_dispatch_click(lam_ui_div_t *root, int32_t px, int32_t py)
{
	lam_ui_div_t *hit = lam_ui_hit_test(root, px, py);
	if (!hit) return 0;
	return lam_bud_dispatch_event(hit, "click", NULL);
}

#ifdef __cplusplus
}
#endif

#endif /* LAM_BUD_H */
