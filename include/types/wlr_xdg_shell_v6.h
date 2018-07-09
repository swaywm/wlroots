#ifndef TYPES_WLR_XDG_SHELL_V6_H
#define TYPES_WLR_XDG_SHELL_V6_H

#include <wayland-server.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include "xdg-shell-unstable-v6-protocol.h"

struct wlr_xdg_positioner_v6_resource {
	struct wl_resource *resource;
	struct wlr_xdg_positioner_v6 attrs;
};

extern const struct wlr_surface_role xdg_toplevel_v6_surface_role;
extern const struct wlr_surface_role xdg_popup_v6_surface_role;

uint32_t schedule_xdg_surface_v6_configure(struct wlr_xdg_surface_v6 *surface);
struct wlr_xdg_surface_v6 *create_xdg_surface_v6(
	struct wlr_xdg_client_v6 *client, struct wlr_surface *surface,
	uint32_t id);
void unmap_xdg_surface_v6(struct wlr_xdg_surface_v6 *surface);
void destroy_xdg_surface_v6(struct wlr_xdg_surface_v6 *surface);
void handle_xdg_surface_v6_commit(struct wlr_surface *wlr_surface);
void handle_xdg_surface_v6_precommit(struct wlr_surface *wlr_surface);

void create_xdg_positioner_v6(struct wlr_xdg_client_v6 *client, uint32_t id);
struct wlr_xdg_positioner_v6_resource *get_xdg_positioner_v6_from_resource(
	struct wl_resource *resource);

void create_xdg_popup_v6(struct wlr_xdg_surface_v6 *xdg_surface,
	struct wlr_xdg_surface_v6 *parent,
	struct wlr_xdg_positioner_v6_resource *positioner, int32_t id);
void handle_xdg_surface_v6_popup_committed(struct wlr_xdg_surface_v6 *surface);
struct wlr_xdg_popup_grab_v6 *get_xdg_shell_v6_popup_grab_from_seat(
	struct wlr_xdg_shell_v6 *shell, struct wlr_seat *seat);
void destroy_xdg_popup_v6(struct wlr_xdg_surface_v6 *surface);

void create_xdg_toplevel_v6(struct wlr_xdg_surface_v6 *xdg_surface,
	uint32_t id);
void handle_xdg_surface_v6_toplevel_committed(struct wlr_xdg_surface_v6 *surface);
void send_xdg_toplevel_v6_configure(struct wlr_xdg_surface_v6 *surface,
	struct wlr_xdg_surface_v6_configure *configure);
void handle_xdg_toplevel_v6_ack_configure(struct wlr_xdg_surface_v6 *surface,
	struct wlr_xdg_surface_v6_configure *configure);
bool compare_xdg_surface_v6_toplevel_state(struct wlr_xdg_toplevel_v6 *state);
void destroy_xdg_toplevel_v6(struct wlr_xdg_surface_v6 *surface);

#endif
