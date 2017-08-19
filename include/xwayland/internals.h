#ifndef XWAYLAND_INTERNALS_H
#define XWAYLAND_INTERNALS_H

void unlink_sockets(int display);
int open_display_sockets(int socks[2]);
#endif
