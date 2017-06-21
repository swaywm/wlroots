#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <wlr/util/list.h>

list_t *list_create(void) {
	list_t *list = malloc(sizeof(list_t));
	list->capacity = 10;
	list->length = 0;
	list->items = malloc(sizeof(void*) * list->capacity);
	return list;
}

static void list_resize(list_t *list) {
	if (list->length == list->capacity) {
		list->capacity += 10;
		list->items = realloc(list->items, sizeof(void*) * list->capacity);
	}
}

void list_free(list_t *list) {
	if (list == NULL) {
		return;
	}
	free(list->items);
	free(list);
}

void list_foreach(list_t *list, void (*callback)(void *item)) {
	if (list == NULL || callback == NULL) {
		return;
	}
	for (size_t i = 0; i < list->length; i++) {
		callback(list->items[i]);
	}
}

void list_add(list_t *list, void *item) {
	list_resize(list);
	list->items[list->length++] = item;
}

void list_push(list_t *list, void *item) {
	list_add(list, item);
}

void list_insert(list_t *list, size_t index, void *item) {
	list_resize(list);
	memmove(&list->items[index + 1], &list->items[index], sizeof(void*) * (list->length - index));
	list->length++;
	list->items[index] = item;
}

void list_del(list_t *list, size_t index) {
	list->length--;
	memmove(&list->items[index], &list->items[index + 1], sizeof(void*) * (list->length - index));
}

void *list_pop(list_t *list) {
	void *_ = list->items[list->length - 1];
	list_del(list, list->length - 1);
	return _;
}

void *list_peek(list_t *list) {
	return list->items[list->length - 1];
}

void list_cat(list_t *list, list_t *source) {
	size_t i;
	for (i = 0; i < source->length; ++i) {
		list_add(list, source->items[i]);
	}
}

void list_qsort(list_t* list, int compare(const void *left, const void *right)) {
	qsort(list->items, list->length, sizeof(void *), compare);
}

int list_seq_find(list_t *list,
		int compare(const void *item, const void *data),
		const void *data) {
	for (size_t i = 0; i < list->length; i++) {
		void *item = list->items[i];
		if (compare(item, data) == 0) {
			return i;
		}
	}
	return -1;
}
