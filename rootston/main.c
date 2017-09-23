#include <wayland-server.h>
#include <wlr/util/log.h>
#include "rootston/config.h"
#include "rootston/server.h"

struct rootston root = { 0 };

int main(int argc, char **argv) {
	root.config = parse_args(argc, argv);
	root.wl_display = wl_display_create();
	root.wl_event_loop = wl_display_get_event_loop(root.wl_display);
	wl_display_init_shm(root.wl_display);
	return 0;
}
