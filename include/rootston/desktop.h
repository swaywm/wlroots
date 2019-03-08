#ifndef ROOTSTON_DESKTOP_H
#define ROOTSTON_DESKTOP_H
#include <time.h>
#include <wayland-server.h>
#include <wlr/config.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_gamma_control.h>
#include <wlr/types/wlr_gtk_primary_selection.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_input_inhibitor.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_list.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_screenshooter.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "rootston/config.h"
#include "rootston/output.h"
#include "rootston/view.h"

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
	struct wlr_xdg_shell *xdg_shell;
	struct wlr_gamma_control_manager *gamma_control_manager;
	struct wlr_gamma_control_manager_v1 *gamma_control_manager_v1;
	struct wlr_screenshooter *screenshooter;
	struct wlr_export_dmabuf_manager_v1 *export_dmabuf_manager_v1;
	struct wlr_server_decoration_manager *server_decoration_manager;
	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
	struct wlr_gtk_primary_selection_device_manager *primary_selection_device_manager;
	struct wlr_idle *idle;
	struct wlr_idle_inhibit_manager_v1 *idle_inhibit;
	struct wlr_input_inhibit_manager *input_inhibit;
	struct wlr_layer_shell_v1 *layer_shell;
	struct wlr_input_method_manager_v2 *input_method;
	struct wlr_text_input_manager_v3 *text_input;
	struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard;
	struct wlr_screencopy_manager_v1 *screencopy;
	struct wlr_tablet_manager_v2 *tablet_v2;
	struct wlr_pointer_constraints_v1 *pointer_constraints;
	struct wlr_presentation *presentation;
	struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager_v1;
	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;
	struct wlr_pointer_gestures_v1 *pointer_gestures;
	struct wlr_output_manager_v1 *output_manager_v1;

	struct wl_listener new_output;
	struct wl_listener layout_change;
	struct wl_listener xdg_shell_v6_surface;
	struct wl_listener xdg_shell_surface;
	struct wl_listener wl_shell_surface;
	struct wl_listener layer_shell_surface;
	struct wl_listener xdg_toplevel_decoration;
	struct wl_listener input_inhibit_activate;
	struct wl_listener input_inhibit_deactivate;
	struct wl_listener virtual_keyboard_new;
	struct wl_listener pointer_constraint;
	struct wl_listener output_manager_apply;
	struct wl_listener output_manager_test;

#if WLR_HAS_XWAYLAND
	struct wlr_xwayland *xwayland;
	struct wl_listener xwayland_surface;
#endif
};

struct roots_server;

struct roots_desktop *desktop_create(struct roots_server *server,
	struct roots_config *config);
void desktop_destroy(struct roots_desktop *desktop);
struct roots_output *desktop_output_from_wlr_output(
	struct roots_desktop *desktop, struct wlr_output *output);

struct wlr_surface *desktop_surface_at(struct roots_desktop *desktop,
		double lx, double ly, double *sx, double *sy,
		struct roots_view **view);

void handle_xdg_shell_v6_surface(struct wl_listener *listener, void *data);
void handle_xdg_shell_surface(struct wl_listener *listener, void *data);
void handle_xdg_toplevel_decoration(struct wl_listener *listener, void *data);
void handle_wl_shell_surface(struct wl_listener *listener, void *data);
void handle_layer_shell_surface(struct wl_listener *listener, void *data);
void handle_xwayland_surface(struct wl_listener *listener, void *data);

#endif
