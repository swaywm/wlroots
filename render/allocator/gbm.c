#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <drm_fourcc.h>
#include <gbm.h>
#include <wayland-server.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/allocator/gbm.h>
#include <wlr/render/allocator/interface.h>
#include <wlr/util/log.h>

static const struct wlr_allocator_impl wlr_gbm_allocator_impl;

static struct wlr_gbm_allocator *wlr_gbm_allocator(struct wlr_allocator *alloc) {
	assert(alloc->impl == &wlr_gbm_allocator_impl);
	return (struct wlr_gbm_allocator *)alloc;
}

static struct wlr_image *wlr_gbm_allocate(struct wlr_allocator *base,
		struct wlr_backend *backend, uint32_t width, uint32_t height,
		uint32_t format, size_t num_modifiers, const uint64_t *modifiers) {
	struct wlr_gbm_allocator *alloc = wlr_gbm_allocator(base);

	struct stat st;
	if (fstat(wlr_backend_get_render_fd(backend), &st)) {
		wlr_log_errno(WLR_ERROR, "Stat failed");
		return NULL;
	}

	if (minor(st.st_rdev) != alloc->minor) {
		wlr_log(WLR_ERROR, "Backend is not compatible with this GBM device");
		return NULL;
	}

	struct wlr_gbm_image *img = calloc(1, sizeof(*img));
	if (!img) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	img->base.backend = backend;
	img->base.width = width;
	img->base.height = height;
	img->base.format = format;
	wl_signal_init(&img->base.release);

	bool is_linear = num_modifiers == 1 &&
		modifiers[0] == DRM_FORMAT_MOD_LINEAR;

	if (!is_linear && num_modifiers) {
		img->bo = gbm_bo_create_with_modifiers(alloc->gbm,
			width, height, format, modifiers, num_modifiers);
	} else {
		uint32_t flags = GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT;
		if (is_linear) {
			flags |= GBM_BO_USE_LINEAR;
		}

		img->bo = gbm_bo_create(alloc->gbm, width, height, format, flags);
	}

	if (!img->bo) {
		wlr_log_errno(WLR_ERROR, "Failed to create GBM bo");
		goto error_img;
	}

	img->base.modifier = gbm_bo_get_modifier(img->bo);

	if (alloc->create && !alloc->create(alloc->userdata, img)) {
		wlr_log_errno(WLR_ERROR, "Renderer could not use GBM bo");
		goto error_bo;
	}

	if (!wlr_backend_attach_gbm(backend, img)) {
		wlr_log_errno(WLR_ERROR, "Backend could not attach to GBM bo");
		goto error_renderer;
	}

	return &img->base;

error_renderer:
	if (alloc->destroy) {
		alloc->destroy(alloc->userdata, img);
	}
error_bo:
	gbm_bo_destroy(img->bo);
error_img:
	free(img);
	return NULL;
}

static void wlr_gbm_deallocate(struct wlr_allocator *base,
		struct wlr_image *img_base) {
	struct wlr_gbm_allocator *alloc = wlr_gbm_allocator(base);
	struct wlr_gbm_image *img = (struct wlr_gbm_image *)img_base;

	wlr_backend_detach_gbm(img->base.backend, img);
	if (alloc->destroy) {
		alloc->destroy(alloc->userdata, img);
	}
	gbm_bo_destroy(img->bo);
	free(img);
}

static const struct wlr_allocator_impl wlr_gbm_allocator_impl = {
	.allocate = wlr_gbm_allocate,
	.deallocate = wlr_gbm_deallocate,
};

struct wlr_gbm_allocator *wlr_gbm_allocator_create(int render_fd, void *userdata,
		wlr_gbm_create_func_t create, wlr_gbm_destroy_func_t destroy) {
	struct wlr_gbm_allocator *alloc = calloc(1, sizeof(*alloc));
	if (!alloc) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	wlr_allocator_init(&alloc->base, &wlr_gbm_allocator_impl);

	alloc->userdata = userdata;
	alloc->create = create;
	alloc->destroy = destroy;

	alloc->gbm = gbm_create_device(render_fd);
	if (!alloc->gbm) {
		wlr_log_errno(WLR_ERROR, "Failed to create GBM device");
		goto error_alloc;
	}

	struct stat st;
	if (fstat(render_fd, &st) < 0) {
		wlr_log_errno(WLR_ERROR, "Stat failed");
		goto error_gbm;
	}

	alloc->minor = minor(st.st_rdev);

	return alloc;

error_gbm:
	gbm_device_destroy(alloc->gbm);
error_alloc:
	free(alloc);
	return NULL;
}
