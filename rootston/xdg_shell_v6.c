#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/log.h>
#include "rootston/desktop.h"
#include "rootston/server.h"

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, destroy);
	wl_list_remove(&roots_xdg_surface->destroy.link);
	wl_list_remove(&roots_xdg_surface->ping_timeout.link);
	wl_list_remove(&roots_xdg_surface->request_move.link);
	wl_list_remove(&roots_xdg_surface->request_resize.link);
	wl_list_remove(&roots_xdg_surface->request_show_window_menu.link);
	wl_list_remove(&roots_xdg_surface->request_minimize.link);
	view_destroy(roots_xdg_surface->view);
	free(roots_xdg_surface);
}

void handle_xdg_shell_v6_surface(struct wl_listener *listener, void *data) {
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, xdg_shell_v6_surface);

	struct wlr_xdg_surface_v6 *surface = data;
	wlr_log(L_DEBUG, "new xdg surface: title=%s, app_id=%s",
		surface->title, surface->app_id);
	wlr_xdg_surface_v6_ping(surface);

	struct roots_xdg_surface_v6 *roots_surface =
		calloc(1, sizeof(struct roots_xdg_surface_v6));
	// TODO: all of the trimmings
	wl_list_init(&roots_surface->destroy.link);
	roots_surface->destroy.notify = handle_destroy;
	wl_signal_add(&surface->events.destroy, &roots_surface->destroy);
	wl_list_init(&roots_surface->ping_timeout.link);
	wl_list_init(&roots_surface->request_minimize.link);
	wl_list_init(&roots_surface->request_move.link);
	wl_list_init(&roots_surface->request_resize.link);
	wl_list_init(&roots_surface->request_show_window_menu.link);

	struct roots_view *view = calloc(1, sizeof(struct roots_view));
	view->type = ROOTS_XDG_SHELL_V6_VIEW;
	view->x = view->y = 200;
	view->xdg_surface_v6 = surface;
	view->roots_xdg_surface_v6 = roots_surface;
	view->wlr_surface = surface->surface;
	roots_surface->view = view;
	wl_list_insert(&desktop->views, &view->link);
}
