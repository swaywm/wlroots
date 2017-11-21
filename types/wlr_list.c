#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <sys/types.h>
#include <wlr/types/wlr_list.h>

bool wlr_list_init(struct wlr_list *list) {
	list->capacity = 10;
	list->length = 0;
	list->items = malloc(sizeof(void *) * list->capacity);
	if (list->items == NULL) {
		return false;
	}
	return true;
}

static bool list_resize(struct wlr_list *list) {
	if (list->length == list->capacity) {
		void *new_items = realloc(list->items,
			sizeof(void *) * (list->capacity + 10));
		if (!new_items) {
			return false;
		}
		list->capacity += 10;
		list->items = new_items;
	}
	return true;
}

void wlr_list_finish(struct wlr_list *list) {
	free(list->items);
}

void wlr_list_for_each(struct wlr_list *list, void (*callback)(void *item)) {
	for (size_t i = 0; i < list->length; i++) {
		callback(list->items[i]);
	}
}

ssize_t wlr_list_push(struct wlr_list *list, void *item) {
	if (!list_resize(list)) {
		return -1;
	}
	list->items[list->length++] = item;
	return list->length;
}

ssize_t wlr_list_insert(struct wlr_list *list, size_t index, void *item) {
	if (!list_resize(list)) {
		return -1;
	}
	memmove(&list->items[index + 1], &list->items[index],
		sizeof(void *) * (list->length - index));
	list->length++;
	list->items[index] = item;
	return list->length;
}

void wlr_list_del(struct wlr_list *list, size_t index) {
	list->length--;
	memmove(&list->items[index], &list->items[index + 1],
		sizeof(void *) * (list->length - index));
}

void *wlr_list_pop(struct wlr_list *list) {
	if (list->length == 0) {
		return NULL;
	}
	void *last = list->items[list->length - 1];
	wlr_list_del(list, list->length - 1);
	return last;
}

void *wlr_list_peek(struct wlr_list *list) {
	if (list->length == 0) {
		return NULL;
	}
	return list->items[list->length - 1];
}

ssize_t wlr_list_cat(struct wlr_list *list, const struct wlr_list *source) {
	size_t old_len = list->length;
	size_t i;
	for (i = 0; i < source->length; ++i) {
		if (wlr_list_push(list, source->items[i]) == -1) {
			list->length = old_len;
			return -1;
		}
	}
	return list->length;
}

void wlr_list_qsort(struct wlr_list *list,
		int compare(const void *left, const void *right)) {
	qsort(list->items, list->length, sizeof(void *), compare);
}

ssize_t wlr_list_find(struct wlr_list *list,
		int compare(const void *item, const void *data), const void *data) {
	for (size_t i = 0; i < list->length; i++) {
		void *item = list->items[i];
		if (compare(item, data) == 0) {
			return i;
		}
	}
	return -1;
}
