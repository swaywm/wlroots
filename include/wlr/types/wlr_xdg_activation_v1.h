/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_XDG_ACTIVATION_V1
#define WLR_TYPES_WLR_XDG_ACTIVATION_V1

#include <wayland-server-core.h>

struct wlr_xdg_activation_token_v1 {
	struct wlr_xdg_activation_v1 *activation;
	// The source surface that created the token.
	struct wlr_surface *surface; // can be NULL
	struct wlr_seat *seat; // can be NULL
	// The serial for the input event that created the token.
	uint32_t serial; // invalid if seat is NULL
	// The application ID to be activated. This is just a hint.
	char *app_id; // can be NULL
	struct wl_list link; // wlr_xdg_activation_v1.tokens

	// private state

	char *token;
	struct wl_resource *resource; // can be NULL
	struct wl_event_source *timeout; // can be NULL

	struct wl_listener seat_destroy;
	struct wl_listener surface_destroy;
};

struct wlr_xdg_activation_v1 {
	uint32_t token_timeout_msec; // token timeout in milliseconds (0 to disable)

	struct wl_list tokens; // wlr_xdg_activation_token_v1.link

	struct {
		struct wl_signal destroy;
		struct wl_signal request_activate; // wlr_xdg_activation_v1_request_activate_event
	} events;

	// private state

	struct wl_global *global;

	struct wl_listener display_destroy;
};

struct wlr_xdg_activation_v1_request_activate_event {
	struct wlr_xdg_activation_v1 *activation;
	// The token used to request activation.
	struct wlr_xdg_activation_token_v1 *token;
	// The surface requesting for activation.
	struct wlr_surface *surface;
	// The startup-id
	const char* id;
};

struct wlr_xdg_activation_v1 *wlr_xdg_activation_v1_create(
	struct wl_display *display);

#endif
