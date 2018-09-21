/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_BACKEND_INTERFACE_H
#define WLR_BACKEND_INTERFACE_H

#include <stdbool.h>
#include <time.h>
#include <wlr/backend.h>
#include <wlr/render/egl.h>

struct wlr_backend_impl {
	bool (*start)(struct wlr_backend *backend);
	void (*destroy)(struct wlr_backend *backend);
	struct wlr_renderer *(*get_renderer)(struct wlr_backend *backend);
	struct wlr_session *(*get_session)(struct wlr_backend *backend);
	clockid_t (*get_presentation_clock)(struct wlr_backend *backend);
	bool (*egl_params)(struct wlr_backend *backend, EGLenum *platform,
		void **remote_display, const EGLint **config_attribs, EGLint *visualid);
	bool (*vulkan_queue_family_present_support)(struct wlr_backend *backend,
		uintptr_t vk_physical_device, uint32_t queue_family);
};

/**
 * Initializes common state on a wlr_backend and sets the implementation to the
 * provided wlr_backend_impl reference.
 */
void wlr_backend_init(struct wlr_backend *backend,
		const struct wlr_backend_impl *impl);

#endif
