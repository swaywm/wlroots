#ifndef WLR_RENDER_TIMELINE_H
#define WLR_RENDER_TIMELINE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * A synchronization timeline.
 *
 * Timelines are used to synchronize accesses to buffers. Given a producer
 * (writing contents to a buffer) and a consumer (reading from the buffer), the
 * compositor needs to synchronize back-and-forth between these two users. The
 * consumer needs to wait for the producer to signal that they're done with the
 * writes, and the producer needs to wait for the consumer to signal that
 * they're done with the reads.
 *
 * Timelines provide synchronization points in the form of monotonically
 * increasing 64-bit integer values.
 *
 * wlroots timelines are designed after Vulkan timeline semaphores. For more
 * information on the Vulkan APIs, see:
 * https://www.khronos.org/blog/vulkan-timeline-semaphores
 *
 * wlroots timelines are powered by DRM synchronization objects (drm_syncobj):
 * https://dri.freedesktop.org/docs/drm/gpu/drm-mm.html#drm-sync-objects
 */
struct wlr_render_timeline;

/**
 * Create a new synchronization timeline.
 */
struct wlr_render_timeline *wlr_render_timeline_create(int drm_fd);
/**
 * Destroy a synchronization timeline.
 */
void wlr_render_timeline_destroy(struct wlr_render_timeline *timeline);
/**
 * Export a timeline point as a sync_file FD.
 *
 * The returned sync_file will be signalled when the provided point is
 * signalled on the timeline.
 *
 * This allows inter-operation with other APIs which don't support drm_syncobj
 * yet. The synchronization point needs to have already materialized:
 * wait-before-signal is not supported.
 */
int wlr_render_timeline_export_sync_file(struct wlr_render_timeline *timeline,
	uint64_t src_point);
/**
 * Import a timeline point from a sync_file FD.
 *
 * The provided timeline point will be signalled when the provided sync_file is.
 *
 * This allows inter-operation with other APIs which don't support drm_syncobj
 * yet.
 */
bool wlr_render_timeline_import_sync_file(struct wlr_render_timeline *timeline,
	uint64_t dst_point, int sync_file_fd);
/**
 * Import a timeline point from a DMA-BUF's implicit fence.
 *
 * The provided timeline point will be signalled when the DMA-BUF's producer
 * has finished their write operations.
 *
 * This allows inter-operation with other APIs which don't support drm_syncobj
 * nor sync_file yet.
 */
bool wlr_render_timeline_import_dmabuf(struct wlr_render_timeline *timeline,
	uint64_t dst_point, int dmabuf_fd);

#endif
