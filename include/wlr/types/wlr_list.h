#ifndef WLR_UTIL_LIST_H
#define WLR_UTIL_LIST_H

#include <stddef.h>

struct wlr_list {
	size_t capacity;
	size_t length;
	void **items;
};

/**
 * Creates a new list, may return `NULL` on failure
 */
struct wlr_list *list_create(void);
void list_free(struct wlr_list *list);
void list_foreach(struct wlr_list *list, void (*callback)(void *item));
/**
 * Add `item` to the end of a list.
 * Returns: new list length or `-1` on failure
 */
int list_add(struct wlr_list *list, void *item);
/**
 * Add `item` to the end of a list.
 * Returns: new list length or `-1` on failure
 */
int list_push(struct wlr_list *list, void *item);
/**
 * Place `item` into index `index` in the list
 * Returns: new list length or `-1` on failure
 */
int list_insert(struct wlr_list *list, size_t index, void *item);
/**
 * Remove an item from the list
 */
void list_del(struct wlr_list *list, size_t index);
/**
 * Remove and return an item from the end of the list
 */
void *list_pop(struct wlr_list *list);
/**
 * Get a reference to the last item of a list without removal
 */
void *list_peek(struct wlr_list *list);
/**
 * Append each item in `source` to `list`
 * Does not modify `source`
 * Returns: new list length or `-1` on failure
 */
int list_cat(struct wlr_list *list, struct wlr_list *source);
// See qsort. Remember to use *_qsort functions as compare functions,
// because they dereference the left and right arguments first!
void list_qsort(struct wlr_list *list, int compare(const void *left, const void *right));
// Return index for first item in list that returns 0 for given compare
// function or -1 if none matches.
int list_seq_find(struct wlr_list *list,
		int compare(const void *item, const void *cmp_to),
		const void *cmp_to);

#endif
