#ifndef _ROOTSTON_DESKTOP_H
#define _ROOTSTON_DESKTOP_H
#include <time.h>
#include <wayland-server.h>
#include "config.h"
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_gamma_control.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_screenshooter.h>
#include <wlr/types/wlr_list.h>
#include "rootston/view.h"
#include "rootston/config.h"

struct roots_output {
	struct roots_desktop *desktop;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct timespec last_frame;
	struct wl_list link; // roots_desktop:outputs
	struct roots_view *fullscreen_view;
};

struct roots_desktop {
	struct wl_list views; // roots_view::link

	struct wl_list outputs; // roots_output::link
	struct timespec last_frame;

	struct roots_server *server;
	struct roots_config *config;

	struct wlr_output_layout *layout;
	struct wlr_xcursor_manager *xcursor_manager;

	struct wlr_compositor *compositor;
	struct wlr_wl_shell *wl_shell;
	struct wlr_xdg_shell_v6 *xdg_shell_v6;
	struct wlr_gamma_control_manager *gamma_control_manager;
	struct wlr_screenshooter *screenshooter;
	struct wlr_server_decoration_manager *server_decoration_manager;
	struct wlr_primary_selection_device_manager *primary_selection_device_manager;

	struct wl_listener output_add;
	struct wl_listener output_remove;
	struct wl_listener xdg_shell_v6_surface;
	struct wl_listener wl_shell_surface;
	struct wl_listener decoration_new;

#ifdef WLR_HAS_XWAYLAND
	struct wlr_xwayland *xwayland;
	struct wl_listener xwayland_surface;
	struct wl_listener xwayland_ready;
#endif
};

struct roots_server;

struct roots_desktop *desktop_create(struct roots_server *server,
		struct roots_config *config);
void desktop_destroy(struct roots_desktop *desktop);
struct roots_output *desktop_output_from_wlr_output(
	struct roots_desktop *desktop, struct wlr_output *output);
struct roots_view *desktop_view_at(struct roots_desktop *desktop, double lx,
	double ly, struct wlr_surface **surface, double *sx, double *sy);

void view_init(struct roots_view *view, struct roots_desktop *desktop);
void view_destroy(struct roots_view *view);
void view_activate(struct roots_view *view, bool activate);

void output_add_notify(struct wl_listener *listener, void *data);
void output_remove_notify(struct wl_listener *listener, void *data);

void handle_xdg_shell_v6_surface(struct wl_listener *listener, void *data);
void handle_wl_shell_surface(struct wl_listener *listener, void *data);
void handle_xwayland_surface(struct wl_listener *listener, void *data);

#endif
