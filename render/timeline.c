#include <xf86drm.h>
#include <stdlib.h>
#include <wlr/render/timeline.h>
#include <wlr/util/log.h>

struct wlr_render_timeline {
	int drm_fd;
	uint32_t handle;
};

struct wlr_render_timeline *wlr_render_timeline_create(int drm_fd) {
	struct wlr_render_timeline *timeline = calloc(1, sizeof(*timeline));
	if (timeline == NULL) {
		return NULL;
	}
	timeline->drm_fd = drm_fd;

	if (drmSyncobjCreate(drm_fd, 0, &timeline->handle) != 0) {
		wlr_log_errno(WLR_ERROR, "drmSyncobjCreate failed");
		free(timeline);
		return NULL;
	}

	return timeline;
}

void wlr_render_timeline_destroy(struct wlr_render_timeline *timeline) {
	drmSyncobjDestroy(timeline->drm_fd, timeline->handle);
	free(timeline);
}

int wlr_render_timeline_export_sync_file(struct wlr_render_timeline *timeline,
		uint64_t src_point) {
	int sync_file_fd = -1;

	uint32_t syncobj_handle;
	if (drmSyncobjCreate(timeline->drm_fd, 0, &syncobj_handle) != 0) {
		wlr_log_errno(WLR_ERROR, "drmSyncobjCreate failed");
		return -1;
	}

	if (drmSyncobjTransfer(timeline->drm_fd, syncobj_handle, 0,
			timeline->handle, src_point, 0) != 0) {
		wlr_log_errno(WLR_ERROR, "drmSyncobjTransfer failed");
		goto out;
	}

	if (drmSyncobjExportSyncFile(timeline->drm_fd,
			syncobj_handle, &sync_file_fd) != 0) {
		wlr_log_errno(WLR_ERROR, "drmSyncobjExportSyncFile failed");
		goto out;
	}

out:
	drmSyncobjDestroy(timeline->drm_fd, syncobj_handle);
	return sync_file_fd;
}

bool wlr_render_timeline_import_sync_file(struct wlr_render_timeline *timeline,
		uint64_t dst_point, int sync_file_fd) {
	bool ok = false;

	uint32_t syncobj_handle;
	if (drmSyncobjCreate(timeline->drm_fd, 0, &syncobj_handle) != 0) {
		wlr_log_errno(WLR_ERROR, "drmSyncobjCreate failed");
		return -1;
	}

	if (drmSyncobjImportSyncFile(timeline->drm_fd, syncobj_handle,
			sync_file_fd) != 0) {
		wlr_log_errno(WLR_ERROR, "drmSyncobjImportSyncFile failed");
		goto out;
	}

	if (drmSyncobjTransfer(timeline->drm_fd, timeline->handle, dst_point,
			syncobj_handle, 0, 0) != 0) {
		wlr_log_errno(WLR_ERROR, "drmSyncobjTransfer failed");
		goto out;
	}

	ok = true;

out:
	drmSyncobjDestroy(timeline->drm_fd, syncobj_handle);
	return ok;
}
