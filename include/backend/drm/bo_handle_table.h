#ifndef BACKEND_DRM_BO_HANDLE_TABLE_H
#define BACKEND_DRM_BO_HANDLE_TABLE_H

/**
 * Table performing reference counting for buffer object handles.
 *
 * The BO handles are allocated incrementally and are recycled by the kernel,
 * so a simple array is used.
 *
 * This design is inspired from amdgpu's code in libdrm:
 * https://gitlab.freedesktop.org/mesa/drm/-/blob/1a4c0ec9aea13211997f982715fe5ffcf19dd067/amdgpu/handle_table.c
 */
struct wlr_drm_bo_handle_table {
	size_t *nrefs;
	size_t len;
};

void drm_bo_handle_table_finish(struct wlr_drm_bo_handle_table *table);
bool drm_bo_handle_table_ref(struct wlr_drm_bo_handle_table *table,
	uint32_t handle);
size_t drm_bo_handle_table_unref(struct wlr_drm_bo_handle_table *table,
	uint32_t handle);

#endif
