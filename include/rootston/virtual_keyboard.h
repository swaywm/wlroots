#ifndef ROOTSTON_VIRTUAL_KEYBOARD_H
#define ROOTSTON_VIRTUAL_KEYBOARD_H

#include <wayland-server-core.h>

void handle_virtual_keyboard(struct wl_listener *listener, void *data);
#endif
