#ifndef _WLR_UTIL_LIST_H
#define _WLR_UTIL_LIST_H

#include <stddef.h>

typedef struct {
	size_t capacity;
	size_t length;
	void **items;
} list_t;

list_t *list_create(void);
void list_free(list_t *list);
void list_foreach(list_t *list, void (*callback)(void* item));
void list_add(list_t *list, void *item);
void list_push(list_t *list, void *item);
void list_insert(list_t *list, size_t index, void *item);
void list_del(list_t *list, size_t index);
void *list_pop(list_t *list);
void *list_peek(list_t *list);
void list_cat(list_t *list, list_t *source);
// See qsort. Remember to use *_qsort functions as compare functions,
// because they dereference the left and right arguments first!
void list_qsort(list_t *list, int compare(const void *left, const void *right));
// Return index for first item in list that returns 0 for given compare
// function or -1 if none matches.
int list_seq_find(list_t *list,
		int compare(const void *item, const void *cmp_to),
		const void *cmp_to);

#endif
