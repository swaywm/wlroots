#ifndef _ROOTSTON_DESKTOP_H
#define _ROOTSTON_DESKTOP_H
#include <time.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/types/wlr_data_device_manager.h>
#include <wlr/types/wlr_gamma_control.h>
#include "rootston/view.h"

struct roots_output {
	struct roots_desktop *desktop;
	struct wlr_output *output;
	struct wl_listener frame;
	struct wl_listener resolution;
	struct timespec last_frame;
	struct wl_list link;
};

struct roots_desktop {
	struct wl_list outputs;
	struct wlr_output_layout *layout;
	struct wlr_compositor *wlr_compositor;
	struct wlr_wl_shell *wl_shell;
	struct wlr_xdg_shell_v6 *xdg_shell;
	struct wlr_data_device_manager *data_device_manager;
	struct wlr_gamma_control_manager *gamma_control_manager;
};

#endif
