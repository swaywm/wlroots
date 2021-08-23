#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include "backend/drm/bo_handle_table.h"

static size_t align(size_t val, size_t align) {
	size_t mask = align - 1;
	return (val + mask) & ~mask;
}

void drm_bo_handle_table_finish(struct wlr_drm_bo_handle_table *table) {
	free(table->nrefs);
}

bool drm_bo_handle_table_ref(struct wlr_drm_bo_handle_table *table,
		uint32_t handle) {
	assert(handle != 0);

	if (handle > table->len) {
		// Grow linearily, because we don't expect the number of BOs to explode
		size_t len = align(handle + 1, 512);
		size_t *nrefs = realloc(table->nrefs, len * sizeof(size_t));
		if (nrefs == NULL) {
			wlr_log_errno(WLR_ERROR, "realloc failed");
			return false;
		}
		memset(&nrefs[table->len], 0, (len - table->len) * sizeof(size_t));
		table->len = len;
		table->nrefs = nrefs;
	}

	table->nrefs[handle]++;
	return true;
}

size_t drm_bo_handle_table_unref(struct wlr_drm_bo_handle_table *table,
		uint32_t handle) {
	assert(handle < table->len);
	assert(table->nrefs[handle] > 0);
	table->nrefs[handle]--;
	return table->nrefs[handle];
}
