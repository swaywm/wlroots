/*
* This an unstable interface of wlroots. No guarantees are made regarding the
* future consistency of this API.
*/
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_SCREENLOCKER_V1_H
#define WLR_TYPES_WLR_SCREENLOCKER_V1_H

struct wlr_screenlock_manager_v1 {
	struct wl_global *global;

	struct wl_list locker_globals;
	struct wl_list lock_surfaces;

	bool locked;

	// This is NULL if a lock was abandoned
	struct wl_resource *lock_resource;

	struct {
		struct wl_signal change_request; // arg: wlr_screenlock_change
	} events;
};

struct wlr_screenlock_lock_surface {
	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wlr_screenlock_manager_v1 *manager;
	struct wl_list link; // wlr_screenlock_manager_v1::lock_surfaces
	struct wl_listener surface_commit;
	struct wl_listener surface_destroy;
	uint32_t current_mode; // enum zwp_screenlocker_visibility_v1.visibility
	uint32_t pending_mode; // enum zwp_screenlocker_visibility_v1.visibility
};

enum wlr_screenlock_mode_change {
	WLR_SCREENLOCK_MODE_CHANGE_LOCK,
	WLR_SCREENLOCK_MODE_CHANGE_UNLOCK,
	WLR_SCREENLOCK_MODE_CHANGE_ABANDON,
	WLR_SCREENLOCK_MODE_CHANGE_RESPAWN, // undo an abandon
	WLR_SCREENLOCK_MODE_CHANGE_REPLACE,
};

struct wlr_screenlock_change {
	struct wl_client *old_client;
	struct wl_client *new_client;
	enum wlr_screenlock_mode_change how;
	bool reject; // only consulted for lock, respawn, and replace
};

struct wlr_screenlock_manager_v1 *wlr_screenlock_manager_v1_create(struct wl_display *display);

/* Signal that the locking animation is complete */
void wlr_screenlock_send_lock_finished(struct wlr_screenlock_manager_v1 *manager);
void wlr_screenlock_send_unlock_finished(struct wlr_screenlock_manager_v1 *manager);

#endif
