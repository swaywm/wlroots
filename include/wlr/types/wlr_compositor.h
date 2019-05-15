/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_COMPOSITOR_H
#define WLR_TYPES_WLR_COMPOSITOR_H

#include <pixman.h>
#include <wayland-server.h>

struct wlr_commit;
struct wlr_output;
struct wlr_surface;

/*
 * This represents a client-side wl_buffer.
 *
 * All of the wl_buffer "factory interfaces" are not managed by wlr_compositor,
 * but we still need to know some extra information beyond the wl_resource
 * handle. This information is retrieved by the callbacks specified in
 * wlr_compositor_buffer_impl.
 *
 * If you are using wlr_renderer, you do not need to worry about this type, as
 * wlr_renderer handles the buffer factory interfaces and the callbacks, but
 * you must call wlr_renderer_set_compositor to set this up.
 */
struct wlr_buffer {
	struct wl_resource *resource;
	size_t ref_cnt;

	/*
	 * This should contain the protocol name of this buffer, used for some
	 * protocol conformance checks (e.g. "wl_shm"). As a special case,
	 * buffers created with the EGL extension "EGL_WL_bind_wayland_display"
	 * will be called "egl", regardless of what the underlying protocol is
	 * called (i.e. NOT "wl_drm"). This should point to statically
	 * allocated memory.
	 */
	const char *protocol;

	/*
	 * The size of the buffer in buffer coordinates.
	 * i.e. no scaling, cropping, or transforms applied.
	 */
	int32_t width;
	int32_t height;
};

/*
 * See struct wlr_buffer
 */
struct wlr_compositor_buffer_impl {
	struct wlr_buffer *(*create)(void *data, struct wlr_buffer *buf,
		pixman_region32_t *damage, struct wl_resource *res);
	struct wlr_buffer *(*ref)(struct wlr_buffer *buf);
	void (*unref)(struct wlr_buffer *buf);
};

/*
 * Implementation of the wl_compositor global.
 *
 * This is responsible for creating wl_surfaces and wl_regions.
 *
 * The wlr_compositor's lifetime to tied to the lifetime of the wl_display
 * it was created with, meaning that calling wl_display_destroy will destroy
 * the wlr_compositor. There is no way to destroy it earlier than this,
 * as clients can not be expected to handle this being removed.
 *
 * Is is an error to create two wlr_compositors for the same wl_display or
 * otherwise advertise a second wl_compositor global on it.
 */
struct wlr_compositor {
	struct wl_display *display;
	struct wl_global *global;

	uint32_t ids;

	const struct wlr_compositor_buffer_impl *buffer_impl;
	void *buffer_data;

	struct {
		struct wl_signal new_surface;
		struct wl_signal new_surface_2;
		struct wl_signal new_state;
	} events;

	struct wl_listener display_destroy;
};

struct wlr_compositor_new_state_args {
	struct wlr_commit *old;
	struct wlr_commit *new;
};

struct wlr_surface_2 {
	struct wl_resource *resource;
	struct wlr_compositor *compositor;

	/*
	 * This should contain the name of the protocol to prevent conflicts.
	 * e.g. "xdg_toplevel".
	 * This should point to statically allocated memory.
	 */
	const char *role_name;
	void *role_data;

	struct wl_list committed; // struct wlr_commit.link
	struct wl_list frame_callbacks; // struct wl_resource

	struct {
		struct wl_signal commit;
		struct wl_signal destroy;
	} events;

	struct wlr_commit *pending;
};

/*
 * A wlr_commit is a description for a "coherent presentation" of a surface's
 * contents, or could also be thought of as a "snaphot" of the surface at
 * commit time. It should contain all of the information to correctly display
 * a surface.
 *
 * A wlr_commit can be extended to add extra state for each commit to be
 * managed properly by the commit queue. Every wayland protocol that has state
 * synchronized by wl_surface.commit should use this extension mechanism.
 *
 * A commit can exist in 3 states:
 *  - Pending:   The client has not commited the contents yet.
 *  - Committed: The client has commited the contents, but something is
 *               inhibiting the presentation of this surface.
 *  - Complete:  The commit is ready to be presented.
 *
 * For a surface, there will always be exactly 1 pending commit for a surface.
 *   see wlr_surface_get_pending
 *
 * Excluding the time before the first complete commit is ready, there will
 * always be at least 1 complete commit for a surface. The latest complete
 * commit is called the current commit.
 *   see wlr_surface_get_commit
 *
 * wlr_commits are reference counted. If you wish to save the contents of a
 * commit for future use, simply do not unreference the commit.
 *
 * Upon commit destruction, several actions may be performed on objects which
 * will affect clients. For example, wl_buffer.release may be called for the
 * attached buffer. Holding onto too many commits for too long may cause
 * resource starvation in clients, so care should be taken to unreference
 * commits as soon as they're no longer needed.
 *
 * Inhibitiors:
 * -----------
 *
 * Each commit has a counter of inhibitors, which means that the commit is
 * being prevented from becoming "complete". The intended use-case is for
 * wl_subsurfaces preventing themselves from being presented until their
 * parent has been commited, and wp_linux_explicit_synchronization from
 * being presented until a fence is signalled.
 *
 * Inhibitors don't have any effect on commits which are already complete.
 *
 * Orphaned Commits:
 * ----------------
 *
 * A commit will stay valid even after the associated surface has been
 * destroyed by the client. Such a commit is called an orphaned commit.
 * Pending commits can never be orphaned, as wl_surface.commit can never be
 * called on them.
 *
 * This can be useful for fade-out animations of a client's surface.
 *
 * Any extension to wlr_commit should be prepared for the associated
 * wlr_surface to not be available.
 */
struct wlr_commit {
	struct wl_list link;
	struct wlr_compositor *compositor;
	struct wlr_surface_2 *surface;

	// If the client has called wl_surface.commit
	bool committed;
	// See wlr_commit_inhibit
	size_t inhibit;
	size_t ref_cnt;

	// wl_surface.attach
	struct wlr_buffer *buffer;
	struct wl_resource *buffer_resource;
	int32_t sx, sy;
	// wl_surface.frame
	struct wl_list frame_callbacks;
	// wl_surface.set_opaque_region
	pixman_region32_t opaque_region;
	// wl_surface.set_input_region
	pixman_region32_t input_region;
	// wl_surface.set_buffer_transform
	enum wl_output_transform transform;
	// wl_surface.set_buffer_scale
	int32_t scale;
	// wl_surface.damage_buffer
	pixman_region32_t damage;

	size_t state_len;
	void **state;

	struct {
		/*
		 * arg: struct wlr_commit *this
		 * The client called wl_surface.commit.
		 */
		struct wl_signal commit;
		/*
		 * arg: struct wlr_commit *this
		 * The commit has transitioned to the complete state, and is
		 * now ready to be presented.
		 */
		struct wl_signal complete;
		/*
		 * arg: struct wlr_commit *this
		 * The wlr_commit has been destroyed.
		 */
		struct wl_signal destroy;
	} events;
};

struct wlr_compositor *wlr_compositor_create(struct wl_display *display);
void wlr_compositor_set_buffer_impl(struct wlr_compositor *comp,
		void *data, const struct wlr_compositor_buffer_impl *impl);
uint32_t wlr_compositor_register(struct wlr_compositor *compositor);

struct wlr_surface_2 *wlr_surface_from_resource_2(struct wl_resource *resource);

/*
 * Get the latest complete commit for a surface.
 * This increases the reference count of the commit.
 *
 * Returns NULL if there is no complete commit yet.
 */
struct wlr_commit *wlr_surface_get_commit(struct wlr_surface_2 *surface);
/*
 * Get the current pending commit for a surface.
 * This DOES NOT increase the reference count of the commit.
 * This is not intended to be used outside of extensions to wlr_commit.
 *
 * This will never fail.
 */
struct wlr_commit *wlr_surface_get_pending(struct wlr_surface_2 *surface);
/*
 * Gets the latest committed or complete commit.
 * This increases the reference count of the commit.
 * This is not intended to be used outside of extensions to wlr_commit.
 *
 * If you are looking for the latest complete commit, use
 * wlr_surface_get_commit instead.
 *
 * Returns NULL if there is no committed/complete commit yet.
 */
struct wlr_commit *wlr_surface_get_latest(struct wlr_surface_2 *surface);

void wlr_surface_send_enter_2(struct wlr_surface_2 *surf, struct wlr_output *output);
void wlr_surface_send_leave_2(struct wlr_surface_2 *surf, struct wlr_output *output);

/*
 * Increases the reference count of a commit.
 * commit must be non-null.
 *
 * commit is returned from this function as a convinience to do
 * `struct wlr_commit *saved = wlr_commit_ref(commit);`
 */
struct wlr_commit *wlr_commit_ref(struct wlr_commit *commit);
/*
 * Decreases the reference count for a commit.
 * commit must be non-null and have a positive reference count.
 *
 * The commit is not necessarily freed immediately when the reference count
 * reaches zero, depending on its state and position in the commit queue.
 */
void wlr_commit_unref(struct wlr_commit *commit);

bool wlr_commit_is_complete(struct wlr_commit *c);

void wlr_commit_inhibit(struct wlr_commit *commit);
void wlr_commit_uninhibit(struct wlr_commit *commit);

bool wlr_surface_set_role_2(struct wlr_surface_2 *surf, const char *name);
void wlr_commit_set(struct wlr_commit *commit, uint32_t id, void *data);
void *wlr_commit_get(struct wlr_commit *commit, uint32_t id);

bool wlr_surface_is_subsurface(struct wlr_surface *surface);

/**
 * Get a subsurface from a surface. Can return NULL if the subsurface has been
 * destroyed.
 */
struct wlr_subsurface *wlr_subsurface_from_wlr_surface(
	struct wlr_surface *surface);

pixman_region32_t *wlr_region_from_resource(struct wl_resource *resource);

#endif
