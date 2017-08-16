#ifndef _WLR_UTIL_LIST_H
#define _WLR_UTIL_LIST_H

#include <stddef.h>

typedef struct {
	size_t capacity;
	size_t length;
	void **items;
} list_t;

/**
 * Creates a new list, may return `NULL` on failure
 */
list_t *list_create(void);
void list_free(list_t *list);
void list_foreach(list_t *list, void (*callback)(void *item));
/**
 * Add `item` to the end of a list.
 * Returns: new list length or `-1` on failure
 */
int list_add(list_t *list, void *item);
/**
 * Add `item` to the end of a list.
 * Returns: new list length or `-1` on failure
 */
int list_push(list_t *list, void *item);
/**
 * Place `item` into index `index` in the list
 * Returns: new list length or `-1` on failure
 */
int list_insert(list_t *list, size_t index, void *item);
/**
 * Remove an item from the list
 */
void list_del(list_t *list, size_t index);
/**
 * Remove and return an item from the end of the list
 */
void *list_pop(list_t *list);
/**
 * Get a reference to the last item of a list without removal
 */
void *list_peek(list_t *list);
/**
 * Append each item in `source` to `list`
 * Does not modify `source`
 * Returns: new list length or `-1` on failure
 */
int list_cat(list_t *list, list_t *source);
// See qsort. Remember to use *_qsort functions as compare functions,
// because they dereference the left and right arguments first!
void list_qsort(list_t *list, int compare(const void *left, const void *right));
// Return index for first item in list that returns 0 for given compare
// function or -1 if none matches.
int list_seq_find(list_t *list,
		int compare(const void *item, const void *cmp_to),
		const void *cmp_to);

#endif
