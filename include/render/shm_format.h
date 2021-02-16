#ifndef RENDER_SHM_FORMAT_H
#define RENDER_SHM_FORMAT_H

#include <wayland-server-protocol.h>

uint32_t convert_wl_shm_format_to_drm(enum wl_shm_format fmt);
enum wl_shm_format convert_drm_format_to_wl_shm(uint32_t fmt);

#endif
