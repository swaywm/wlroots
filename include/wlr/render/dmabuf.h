#ifndef WLR_RENDER_DMABUF_H
#define WLR_RENDER_DMABUF_H

// So we don't have to pull in linux specific drm headers
#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID ((1ULL<<56) - 1)
#endif

#define WLR_DMABUF_MAX_PLANES 4

enum wlr_dmabuf_attributes_flags {
	WLR_DMABUF_ATTRIBUTES_FLAGS_Y_INVERT = 1,
	WLR_DMABUF_ATTRIBUTES_FLAGS_INTERLACED = 2,
	WLR_DMABUF_ATTRIBUTES_FLAGS_BOTTOM_FIRST = 4,
};

struct wlr_dmabuf_attributes {
	int32_t width, height;
	uint32_t format;
	uint32_t flags; // enum wlr_dmabuf_attributes_flags
	uint64_t modifier;

	int n_planes;
	uint32_t offset[WLR_DMABUF_MAX_PLANES];
	uint32_t stride[WLR_DMABUF_MAX_PLANES];
	int fd[WLR_DMABUF_MAX_PLANES];
};

#endif
