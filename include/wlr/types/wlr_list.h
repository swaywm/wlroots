/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_LIST_H
#define WLR_TYPES_WLR_LIST_H

#include <stdbool.h>
#include <stddef.h>

struct wlr_list {
	size_t capacity;
	size_t length;
	void **items;
};

/**
 * Initialize a list. Returns true on success, false on failure.
 */
bool wlr_list_init(struct wlr_list *list);

/**
 * Deinitialize a list.
 */
void wlr_list_finish(struct wlr_list *list);

/**
 * Executes `callback` on each element in the list.
 */
void wlr_list_for_each(struct wlr_list *list, void (*callback)(void *item));

/**
 * Add `item` to the end of a list.
 * Returns: new list length or `-1` on failure.
 */
ssize_t wlr_list_push(struct wlr_list *list, void *item);

/**
 * Place `item` into index `index` in the list.
 * Returns: new list length or `-1` on failure.
 */
ssize_t wlr_list_insert(struct wlr_list *list, size_t index, void *item);

/**
 * Remove an item from the list.
 */
void wlr_list_del(struct wlr_list *list, size_t index);

/**
 * Remove and return an item from the end of the list.
 */
void *wlr_list_pop(struct wlr_list *list);

/**
 * Get a reference to the last item of a list without removal.
 */
void *wlr_list_peek(struct wlr_list *list);

/**
 * Append each item in `source` to `list`.
 * Does not modify `source`.
 * Returns: new list length or `-1` on failure.
 */
ssize_t wlr_list_cat(struct wlr_list *list, const struct wlr_list *source);

/**
 * Sort a list using `qsort`.
 */
void wlr_list_qsort(struct wlr_list *list,
	int compare(const void *left, const void *right));

/**
 * Return the index of the first item in the list that returns 0 for the given
 * `compare` function, or -1 if none matches.
 */
ssize_t wlr_list_find(struct wlr_list *list,
	int compare(const void *item, const void *cmp_to), const void *cmp_to);

#endif
