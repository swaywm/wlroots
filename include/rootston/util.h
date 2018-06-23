#ifndef ROOTSTON_UTIL_H
#define ROOTSTON_UTIL_H

#include <wayland-server.h>

void roots_signal_emit_safe(struct wl_signal *signal, void *data);

#endif
