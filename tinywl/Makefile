WAYLAND_PROTOCOLS=/usr/share/wayland-protocols

xdg-shell-protocol.h:
	wayland-scanner server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

xdg-shell-protocol.c: xdg-shell-protocol.h
	wayland-scanner private-code \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

tinywl: tinywl.c xdg-shell-protocol.h xdg-shell-protocol.c
	$(CC) $(CFLAGS) \
		-g -Werror -I. \
		-DWLR_USE_UNSTABLE \
		$(shell pkg-config --cflags --libs wlroots) \
		$(shell pkg-config --cflags --libs wayland-server) \
		$(shell pkg-config --cflags --libs xkbcommon) \
		-o $@ $<

clean:
	rm -f tinywl xdg-shell-protocol.h xdg-shell-protocol.c

.DEFAULT_GOAL=tinywl
.PHONY: clean
