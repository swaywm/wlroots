#ifndef _ROOTSTON_SERVER_H
#define _ROOTSTON_SERVER_H

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/config.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_data_device.h>
#if WLR_HAS_XWAYLAND
#include <wlr/xwayland.h>
#endif
#include "rootston/config.h"
#include "rootston/desktop.h"
#include "rootston/input.h"

struct roots_server {
	/* Rootston resources */
	struct roots_config *config;
	struct roots_desktop *desktop;
	struct roots_input *input;

	/* Wayland resources */
	struct wl_display *wl_display;
	struct wl_event_loop *wl_event_loop;

	/* WLR tools */
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;

	/* Global resources */
	struct wlr_data_device_manager *data_device_manager;
};

extern struct roots_server server;

#endif
