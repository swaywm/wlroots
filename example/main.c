#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <wlr/backend.h>
#include <wlr/backend/wayland.h>
#include <wayland-server.h>

int main(int argc, char **argv) {
	// TODO: Move this stuff to a wlr backend selector function
	char *_wl_display = getenv("WAYLAND_DISPLAY");
	if (_wl_display) {
		unsetenv("WAYLAND_DISPLAY");
		setenv("_WAYLAND_DISPLAY", _wl_display, 1);
	}
	struct wl_display *wl_display = wl_display_create();
	struct wlr_wl_backend *backend = wlr_wl_backend_init(wl_display, 1);
	wlr_wl_backend_free(backend);
	wl_display_destroy(wl_display);
	return 0;
}
