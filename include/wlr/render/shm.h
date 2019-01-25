#ifndef WLR_RENDER_SHM_H
#define WLR_RENDER_SHM_H

#include <stdint.h>

#include <pixman.h>
#include <wayland-server.h>

struct wlr_format_set;

bool wlr_shm_init(struct wl_display *display, const struct wlr_format_set *fmts);

typedef void (*wlr_shm_write_fn)(void *userdata, const void *data,
	uint32_t stride, const pixman_rectangle32_t *rect);

void wlr_shm_apply_damage(struct wl_shm_buffer *buffer, pixman_region32_t *damage,
	void *userdata, wlr_shm_write_fn write_fn);

#endif
