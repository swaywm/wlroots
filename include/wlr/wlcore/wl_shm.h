#ifndef _WLR_WLCORE_WL_SHM_H
#define _WLR_WLCORE_WL_SHM_H
#include <wayland-server-core.h>
#include <wlr/render.h>

struct wlr_wl_shm;

struct wlr_wl_shm *wlr_wl_shm_init(struct wl_display *display);
void wlr_wl_shm_add_format(struct wlr_wl_shm *shm, enum wl_shm_format format);
void wlr_wl_shm_add_renderer_formats(
		struct wlr_wl_shm *shm, struct wlr_renderer *renderer);

#endif
