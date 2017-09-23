
void cursor_update_position(struct roots_input *input, uint32_t time) {
	/*
	if (input->motion_context.surface) {
		struct example_xdg_surface_v6 *surface;
		surface = sample->motion_context.surface;
		surface->position.lx = sample->cursor->x - sample->motion_context.off_x;
		surface->position.ly = sample->cursor->y - sample->motion_context.off_y;
		return;
	}
	*/

	struct wlr_xdg_surface_v6 *surface = example_xdg_surface_at(sample,
			sample->cursor->x, sample->cursor->y);

	if (surface) {
		struct example_xdg_surface_v6 *esurface = surface->data;

		double sx = sample->cursor->x - esurface->position.lx;
		double sy = sample->cursor->y - esurface->position.ly;

		// TODO z-order
		wlr_seat_pointer_enter(sample->wl_seat, surface->surface, sx, sy);
		wlr_seat_pointer_send_motion(sample->wl_seat, time, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(sample->wl_seat);
	}
}
