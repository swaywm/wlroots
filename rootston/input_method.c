#include <wlr/util/log.h>
#include "rootston/input_method.h"
#include "rootston/seat.h"

static void update_maximized_views(struct roots_desktop *desktop) {
	struct roots_view *v;
	wl_list_for_each(v, &desktop->views, link) {
		if (v->maximized) {
			view_arrange_maximized(v);
		}
	}
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	struct roots_input_panel_surface *roots_surface =
		wl_container_of(listener, roots_surface, surface_commit);
	struct roots_view *view = roots_surface->view;
	struct wlr_input_panel_surface *surface = view->input_panel_surface;

// let kb choose its preferred size for now (resize manually later)
	int width = surface->surface->current->width;
	int height = surface->surface->current->height;

	view_update_size(view, width, height);
	view_apply_damage(view);
	update_maximized_views(view->desktop);
}

static void handle_surface_destroy(struct wl_listener *listener, void *data) {
	struct roots_input_panel_surface *roots_surface =
		wl_container_of(listener, roots_surface, destroy);
	struct roots_view *view = roots_surface->view;
	roots_surface->view = NULL;
	// FIXME: check if mapped first
	struct roots_desktop *desktop = NULL;
	if (view->wlr_surface != NULL) {
		desktop = view->desktop;
	}
	view_destroy(view);
	if (desktop) {
		update_maximized_views(desktop);
	}
	free(roots_surface);
}

static void show_input_panel_surface(struct roots_desktop *desktop,
		struct wlr_input_panel_surface *surface) {
	struct roots_input_panel_surface *roots_surface =
		calloc(1, sizeof(struct roots_input_panel_surface));
	if (!roots_surface) {
		return;
	}
	roots_surface->destroy.notify = handle_surface_destroy;
	wl_signal_add(&surface->events.destroy, &roots_surface->destroy);

	roots_surface->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&surface->surface->events.commit, &roots_surface->surface_commit);

	struct roots_view *view = view_create(desktop);
	if (!view) {
		free(roots_surface);
		return;
	}
	view->type = ROOTS_INPUT_PANEL_VIEW;
	view->special = true;
	view->features.frame = ROOTS_FRAME_BOTTOM;

	view->width = surface->surface->current->width;
	view->height = surface->surface->current->height;

	view->input_panel_surface = surface;
	view->roots_input_panel_surface = roots_surface;
	roots_surface->view = view;
	view_map(view, surface->surface);
	//view_setup(view);

	// FIXME: don't render/position the osk here, it must first be requested by
	// something on an output
	struct roots_output *output;
	wl_list_for_each(output, &desktop->outputs, link) {
		break; // whatever, take the 1st one
	}
	// FIXME: needs to receive notifications about removed outputs
	// or when the spatially associated source moves
	view_set_anchor_position(view, output, view->width, view->height);
}


void handle_input_panel_surface(struct wl_listener *listener, void *data) {
	struct wlr_input_panel_surface *surface = data;
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, input_panel_surface);
	wl_list_insert(&desktop->input_panel->surfaces, &surface->link);
}

void handle_input_method_context(struct wl_listener *listener, void *data) {
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, input_method_context);
	struct wlr_input_method_context *context = data;

	// FIXME: assign correct seat
	char *seat_name = ROOTS_CONFIG_DEFAULT_SEAT_NAME;
	struct roots_seat *seat = input_get_seat(desktop->server->input, seat_name);
	//struct roots_seat *seat = input_seat_from_wlr_seat(desktop->server->input, context->im->seat);
	if (!seat) {
		wlr_log(L_ERROR, "could not create roots seat");
		return;
	}

	roots_seat_add_device(seat, &context->input_device);

	struct wlr_input_panel_surface *surface;
	wl_list_for_each(surface, &desktop->input_panel->surfaces, link) {
		show_input_panel_surface(desktop, surface);
	}

	wl_signal_add(&context->events.destroy,
		&desktop->input_method_context_destroy);
}

void handle_input_method_context_destroy(struct wl_listener *listener,
		void *data) {
	//struct wlr_input_method_context *context = data;
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, input_method_context_destroy);

	// FIXME: attach the views to the input method and single them out
	struct roots_view *view, *tmp;
	wl_list_for_each_safe(view, tmp, &desktop->views, link) {
		if (view->type == ROOTS_INPUT_PANEL_VIEW) {
				//&& view->roots_input_panel_surface->input_method.seat == seat
			view_unmap(view);
		}
	}
	update_maximized_views(desktop);
}
