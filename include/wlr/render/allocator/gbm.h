#ifndef WLR_RENDER_ALLOCATOR_GBM_H
#define WLR_RENDER_ALLOCATOR_GBM_H

#include <stdint.h>
#include <gbm.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>

struct wlr_gbm_image {
	struct wlr_image base;

	struct gbm_bo *bo;
	void *renderer_priv;
};

struct wlr_gbm_allocator_impl {
	bool (*create)(void *userdata, struct wlr_gbm_image *img);
	void (*destroy)(void *userdata, struct wlr_gbm_image *img);
};

typedef bool (*wlr_gbm_create_func_t)(void *, struct wlr_gbm_image *);
typedef void (*wlr_gbm_destroy_func_t)(void *, struct wlr_gbm_image *);

struct wlr_gbm_allocator {
	struct wlr_allocator base;
	struct gbm_device *gbm;

	unsigned minor;

	void *userdata;
	wlr_gbm_create_func_t create;
	wlr_gbm_destroy_func_t destroy;
};

struct wlr_gbm_allocator *wlr_gbm_allocator_create(int render_fd, void *userdata,
	wlr_gbm_create_func_t create, wlr_gbm_destroy_func_t destroy);

void wlr_gbm_allocator_destroy(struct wlr_gbm_allocator *alloc);

#endif
