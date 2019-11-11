/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_IFACES_WLR_BUFFER_H
#define WLR_IFACES_WLR_BUFFER_H
#include <stdbool.h>
#include <pixman.h>
#include <wayland-server-core.h>
#include <wlr/render/wlr_renderer.h>

struct wlr_buffer;

struct wlr_buffer_impl {
	/**
	 * Return true if this wl_resource is an instance of a wl_buffer using this
	 * API.
	 *
	 * Note: downstream compositors should not call this. We have to
	 * special-case some buffer types and it will not necessarily work the way
	 * you expect.
	 */
	bool (*is_instance)(struct wl_resource *resource);

	/**
	 * Initializes the buffer from a resource. This generally involves
	 * populating wlr_buffer.texture, which is required to render it with
	 * wlr_renderer. However, users with custom renderers may choose to leave it
	 * NULL and process the buffer data some other way.
	 *
	 * Return false if an error occured and the resulting buffer is invalid.
	 */
	bool (*initialize)(struct wlr_buffer *buffer,
			struct wl_resource *resource, struct wlr_renderer *renderer);
	/**
	 * Gets the width and height of this buffer in pixels. This is used for
	 * wl_surface lifecycle management, and possibly by downstream compositors.
	 * Return false if size is unknown, true otherwise. Set to NULL if
	 * unsupported for this buffer type.
	 */
	bool (*get_resource_size)(struct wl_resource *resource,
		struct wlr_renderer *renderer, int *width, int *height);

	/**
	 * Apply damage to this buffer, if supported. This is used, for example, by
	 * shm buffers to upload only damaged pixels to the GPU. The resource
	 * parameter is the wl_resource of the buffer to source new data from, it
	 * may be the same resource as the buffer was created with or another. It
	 * will be the same buffer implementation as the buffer parameter. The
	 * underlying buffer resource will be updated to the resource parameter if
	 * successful.
	 *
	 * Returns true if successful. Leave NULL if unsupported for this buffer
	 * type.
	 *
	 * This is not called if the buffer has more than one reference.
	 */
	bool (*apply_damage)(struct wlr_buffer *buffer,
			struct wl_resource *resource, pixman_region32_t *damage);

	/**
	 * Called when there are no remaining references to this buffer. The texture
	 * will be destroyed for you, you may leave this NULL unless you have any
	 * other work to do.
	 */
	void (*destroy)(struct wlr_buffer *buffer);
};

void wlr_buffer_register_implementation(const struct wlr_buffer_impl *impl);

#endif
