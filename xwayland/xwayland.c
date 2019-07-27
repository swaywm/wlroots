#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>
#include "sockets.h"
#include "util/signal.h"
#include "xwayland/xwm.h"

struct wlr_xwayland_cursor {
	uint8_t *pixels;
	uint32_t stride;
	uint32_t width;
	uint32_t height;
	int32_t hotspot_x;
	int32_t hotspot_y;
};

static void safe_close(int fd) {
	if (fd >= 0) {
		close(fd);
	}
}

static int fill_arg(char ***argv, const char *fmt, ...) {
	int len;
	char **cur_arg = *argv;
	va_list args;
	va_start(args, fmt);
	len = vsnprintf(NULL, 0, fmt, args) + 1;
	va_end(args);
	while (*cur_arg) {
		cur_arg++;
	}
	*cur_arg = malloc(len);
	if (!*cur_arg) {
		return -1;
	}
	*argv = cur_arg;
	va_start(args, fmt);
	len = vsnprintf(*cur_arg, len, fmt, args);
	va_end(args);
	return len;
}

_Noreturn static void exec_xwayland(struct wlr_xwayland *wlr_xwayland) {
	if (!set_cloexec(wlr_xwayland->x_fd[0], false) ||
			!set_cloexec(wlr_xwayland->x_fd[1], false) ||
			!set_cloexec(wlr_xwayland->wm_fd[1], false) ||
			!set_cloexec(wlr_xwayland->wl_fd[1], false)) {
		_exit(EXIT_FAILURE);
	}

	/* Make Xwayland signal us when it's ready */
	signal(SIGUSR1, SIG_IGN);

	char *argv[] = {
		"Xwayland", NULL /* display, e.g. :1 */,
		"-rootless", "-terminate",
		"-listen", NULL /* x_fd[0] */,
		"-listen", NULL /* x_fd[1] */,
		"-wm", NULL /* wm_fd[1] */,
		NULL,
	};
	char **cur_arg = argv;

	if (fill_arg(&cur_arg, ":%d", wlr_xwayland->display) < 0 ||
			fill_arg(&cur_arg, "%d", wlr_xwayland->x_fd[0]) < 0 ||
			fill_arg(&cur_arg, "%d", wlr_xwayland->x_fd[1]) < 0 ||
			fill_arg(&cur_arg, "%d", wlr_xwayland->wm_fd[1]) < 0) {
		wlr_log_errno(WLR_ERROR, "alloc/print failure");
		_exit(EXIT_FAILURE);
	}

	char wayland_socket_str[16];
	snprintf(wayland_socket_str, sizeof(wayland_socket_str), "%d", wlr_xwayland->wl_fd[1]);
	setenv("WAYLAND_SOCKET", wayland_socket_str, true);

	wlr_log(WLR_INFO, "WAYLAND_SOCKET=%d Xwayland :%d -rootless -terminate -listen %d -listen %d -wm %d",
		wlr_xwayland->wl_fd[1], wlr_xwayland->display, wlr_xwayland->x_fd[0],
		wlr_xwayland->x_fd[1], wlr_xwayland->wm_fd[1]);

	// Closes stdout/stderr depending on log verbosity
	enum wlr_log_importance verbosity = wlr_log_get_verbosity();
	int devnull = open("/dev/null", O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
	if (devnull < 0) {
		wlr_log_errno(WLR_ERROR, "XWayland: failed to open /dev/null");
		_exit(EXIT_FAILURE);
	}
	if (verbosity < WLR_INFO) {
		dup2(devnull, STDOUT_FILENO);
	}
	if (verbosity < WLR_ERROR) {
		dup2(devnull, STDERR_FILENO);
	}

	// This returns if and only if the call fails
	execvp("Xwayland", argv);

	wlr_log_errno(WLR_ERROR, "failed to exec Xwayland");
	close(devnull);
	_exit(EXIT_FAILURE);
}

static void xwayland_finish_server(struct wlr_xwayland *wlr_xwayland) {
	if (!wlr_xwayland || wlr_xwayland->display == -1) {
		return;
	}

	if (wlr_xwayland->x_fd_read_event[0]) {
		wl_event_source_remove(wlr_xwayland->x_fd_read_event[0]);
		wl_event_source_remove(wlr_xwayland->x_fd_read_event[1]);

		wlr_xwayland->x_fd_read_event[0] = wlr_xwayland->x_fd_read_event[1] = NULL;
	}

	if (wlr_xwayland->cursor != NULL) {
		free(wlr_xwayland->cursor);
	}

	xwm_destroy(wlr_xwayland->xwm);

	if (wlr_xwayland->client) {
		wl_list_remove(&wlr_xwayland->client_destroy.link);
		wl_client_destroy(wlr_xwayland->client);
	}
	if (wlr_xwayland->sigusr1_source) {
		wl_event_source_remove(wlr_xwayland->sigusr1_source);
	}

	safe_close(wlr_xwayland->wl_fd[0]);
	safe_close(wlr_xwayland->wl_fd[1]);
	safe_close(wlr_xwayland->wm_fd[0]);
	safe_close(wlr_xwayland->wm_fd[1]);
	memset(wlr_xwayland, 0, offsetof(struct wlr_xwayland, display));
	wlr_xwayland->wl_fd[0] = wlr_xwayland->wl_fd[1] = -1;
	wlr_xwayland->wm_fd[0] = wlr_xwayland->wm_fd[1] = -1;

	/* We do not kill the Xwayland process, it dies to broken pipe
	 * after we close our side of the wm/wl fds. This is more reliable
	 * than trying to kill something that might no longer be Xwayland.
	 */
}

static void xwayland_finish_display(struct wlr_xwayland *wlr_xwayland) {
	if (!wlr_xwayland || wlr_xwayland->display == -1) {
		return;
	}

	safe_close(wlr_xwayland->x_fd[0]);
	safe_close(wlr_xwayland->x_fd[1]);
	wlr_xwayland->x_fd[0] = wlr_xwayland->x_fd[1] = -1;

	wl_list_remove(&wlr_xwayland->display_destroy.link);

	unlink_display_sockets(wlr_xwayland->display);
	wlr_xwayland->display = -1;
	wlr_xwayland->display_name[0] = '\0';
}

static bool xwayland_start_server(struct wlr_xwayland *wlr_xwayland);
static bool xwayland_start_server_lazy(struct wlr_xwayland *wlr_xwayland);

static void handle_client_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xwayland *wlr_xwayland =
		wl_container_of(listener, wlr_xwayland, client_destroy);

	if (wlr_xwayland->sigusr1_source) {
		// Xwayland failed to start, let the sigusr1 handler deal with it
		return;
	}

	// Don't call client destroy: it's being destroyed already
	wlr_xwayland->client = NULL;
	wl_list_remove(&wlr_xwayland->client_destroy.link);

	xwayland_finish_server(wlr_xwayland);

	if (time(NULL) - wlr_xwayland->server_start > 5) {
		if (wlr_xwayland->lazy) {
			wlr_log(WLR_INFO, "Restarting Xwayland (lazy)");
			xwayland_start_server_lazy(wlr_xwayland);
		} else  {
			wlr_log(WLR_INFO, "Restarting Xwayland");
			xwayland_start_server(wlr_xwayland);
		}
	}
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xwayland *wlr_xwayland =
		wl_container_of(listener, wlr_xwayland, display_destroy);

	// Don't call client destroy: the display is being destroyed, it's too late
	if (wlr_xwayland->client) {
		wlr_xwayland->client = NULL;
		wl_list_remove(&wlr_xwayland->client_destroy.link);
	}

	wlr_xwayland_destroy(wlr_xwayland);
}

static int xserver_handle_ready(int signal_number, void *data) {
	struct wlr_xwayland *wlr_xwayland = data;

	int stat_val = -1;
	while (waitpid(wlr_xwayland->pid, &stat_val, 0) < 0) {
		if (errno == EINTR) {
			continue;
		}
		wlr_log_errno(WLR_ERROR, "waitpid for Xwayland fork failed");
		return 1;
	}
	if (stat_val) {
		wlr_log(WLR_ERROR, "Xwayland startup failed, not setting up xwm");
		return 1;
	}
	wlr_log(WLR_DEBUG, "Xserver is ready");

	wlr_xwayland->xwm = xwm_create(wlr_xwayland);
	if (!wlr_xwayland->xwm) {
		xwayland_finish_server(wlr_xwayland);
		return 1;
	}

	if (wlr_xwayland->seat) {
		xwm_set_seat(wlr_xwayland->xwm, wlr_xwayland->seat);
	}

	wl_event_source_remove(wlr_xwayland->sigusr1_source);
	wlr_xwayland->sigusr1_source = NULL;

	if (wlr_xwayland->cursor != NULL) {
		struct wlr_xwayland_cursor *cur = wlr_xwayland->cursor;
		xwm_set_cursor(wlr_xwayland->xwm, cur->pixels, cur->stride, cur->width,
			cur->height, cur->hotspot_x, cur->hotspot_y);
		free(cur);
		wlr_xwayland->cursor = NULL;
	}


	wlr_signal_emit_safe(&wlr_xwayland->events.ready, wlr_xwayland);
	/* ready is a one-shot signal, fire and forget */
	wl_signal_init(&wlr_xwayland->events.ready);

	return 1; /* wayland event loop dispatcher's count */
}

static int xwayland_socket_connected(int fd, uint32_t mask, void* data){
	struct wlr_xwayland *wlr_xwayland = data;

	wl_event_source_remove(wlr_xwayland->x_fd_read_event[0]);
	wl_event_source_remove(wlr_xwayland->x_fd_read_event[1]);

	wlr_xwayland->x_fd_read_event[0] = wlr_xwayland->x_fd_read_event[1] = NULL;

	xwayland_start_server(wlr_xwayland);

	return 0;
}

static bool xwayland_start_display(struct wlr_xwayland *wlr_xwayland,
		struct wl_display *wl_display) {

	wlr_xwayland->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(wl_display, &wlr_xwayland->display_destroy);

	wlr_xwayland->display = open_display_sockets(wlr_xwayland->x_fd);
	if (wlr_xwayland->display < 0) {
		xwayland_finish_display(wlr_xwayland);
		return false;
	}

	snprintf(wlr_xwayland->display_name, sizeof(wlr_xwayland->display_name),
		":%d", wlr_xwayland->display);
	return true;
}

static bool xwayland_start_server(struct wlr_xwayland *wlr_xwayland) {
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, wlr_xwayland->wl_fd) != 0 ||
			socketpair(AF_UNIX, SOCK_STREAM, 0, wlr_xwayland->wm_fd) != 0) {
		wlr_log_errno(WLR_ERROR, "socketpair failed");
		xwayland_finish_server(wlr_xwayland);
		return false;
	}
	if (!set_cloexec(wlr_xwayland->wl_fd[0], true) ||
			!set_cloexec(wlr_xwayland->wl_fd[1], true) ||
			!set_cloexec(wlr_xwayland->wm_fd[0], true) ||
			!set_cloexec(wlr_xwayland->wm_fd[1], true)) {
		xwayland_finish_server(wlr_xwayland);
		return false;
	}

	wlr_xwayland->server_start = time(NULL);

	wlr_xwayland->client =
		wl_client_create(wlr_xwayland->wl_display, wlr_xwayland->wl_fd[0]);
	if (!wlr_xwayland->client) {
		wlr_log_errno(WLR_ERROR, "wl_client_create failed");
		xwayland_finish_server(wlr_xwayland);
		return false;
	}

	wlr_xwayland->wl_fd[0] = -1; /* not ours anymore */

	wlr_xwayland->client_destroy.notify = handle_client_destroy;
	wl_client_add_destroy_listener(wlr_xwayland->client,
		&wlr_xwayland->client_destroy);

	struct wl_event_loop *loop = wl_display_get_event_loop(wlr_xwayland->wl_display);
	wlr_xwayland->sigusr1_source = wl_event_loop_add_signal(loop, SIGUSR1,
		xserver_handle_ready, wlr_xwayland);

	wlr_xwayland->pid = fork();
	if (wlr_xwayland->pid < 0) {
		wlr_log_errno(WLR_ERROR, "fork failed");
		xwayland_finish_server(wlr_xwayland);
		return false;
	} else if (wlr_xwayland->pid == 0) {
		/* Double-fork, but we need to forward SIGUSR1 once Xserver(1)
		 * is ready, or error if there was one. */
		pid_t ppid = getppid();
		sigset_t sigset;
		sigemptyset(&sigset);
		sigaddset(&sigset, SIGUSR1);
		sigaddset(&sigset, SIGCHLD);
		sigprocmask(SIG_BLOCK, &sigset, NULL);

		pid_t pid = fork();
		if (pid < 0) {
			wlr_log_errno(WLR_ERROR, "second fork failed");
			_exit(EXIT_FAILURE);
		} else if (pid == 0) {
			exec_xwayland(wlr_xwayland);
		}

		int sig;
		sigwait(&sigset, &sig);
		kill(ppid, SIGUSR1);
		wlr_log(WLR_DEBUG, "sent SIGUSR1 to process %d", ppid);
		if (sig == SIGCHLD) {
			waitpid(pid, NULL, 0);
			_exit(EXIT_FAILURE);
		}

		_exit(EXIT_SUCCESS);
	}

	/* close child fds */
	/* remain managing x sockets for lazy start */
	close(wlr_xwayland->wl_fd[1]);
	close(wlr_xwayland->wm_fd[1]);
	wlr_xwayland->wl_fd[1] = wlr_xwayland->wm_fd[1] = -1;

	return true;
}

static bool xwayland_start_server_lazy(struct wlr_xwayland *wlr_xwayland) {
	struct wl_event_loop *loop = wl_display_get_event_loop(wlr_xwayland->wl_display);
	wlr_xwayland->x_fd_read_event[0] =
		wl_event_loop_add_fd(loop, wlr_xwayland->x_fd[0], WL_EVENT_READABLE,
				xwayland_socket_connected, wlr_xwayland);
	wlr_xwayland->x_fd_read_event[1] =
		wl_event_loop_add_fd(loop, wlr_xwayland->x_fd[1], WL_EVENT_READABLE,
				xwayland_socket_connected, wlr_xwayland);

	return true;
}

void wlr_xwayland_destroy(struct wlr_xwayland *wlr_xwayland) {
	if (!wlr_xwayland) {
		return;
	}

	wlr_xwayland_set_seat(wlr_xwayland, NULL);
	xwayland_finish_server(wlr_xwayland);
	xwayland_finish_display(wlr_xwayland);
	free(wlr_xwayland);
}

struct wlr_xwayland *wlr_xwayland_create(struct wl_display *wl_display,
		struct wlr_compositor *compositor, bool lazy) {
	struct wlr_xwayland *wlr_xwayland = calloc(1, sizeof(struct wlr_xwayland));
	if (!wlr_xwayland) {
		return NULL;
	}

	wlr_xwayland->wl_display = wl_display;
	wlr_xwayland->compositor = compositor;
	wlr_xwayland->lazy = lazy;

	wlr_xwayland->x_fd[0] = wlr_xwayland->x_fd[1] = -1;
	wlr_xwayland->wl_fd[0] = wlr_xwayland->wl_fd[1] = -1;
	wlr_xwayland->wm_fd[0] = wlr_xwayland->wm_fd[1] = -1;

	wl_signal_init(&wlr_xwayland->events.new_surface);
	wl_signal_init(&wlr_xwayland->events.ready);

	if (!xwayland_start_display(wlr_xwayland, wl_display)) {
		goto error_alloc;
	}

	if (wlr_xwayland->lazy) {
		if (!xwayland_start_server_lazy(wlr_xwayland)) {
			goto error_display;
		}
	} else {
		if (!xwayland_start_server(wlr_xwayland)) {
			goto error_display;
		}
	}

	return wlr_xwayland;

error_display:
	xwayland_finish_display(wlr_xwayland);

error_alloc:
	free(wlr_xwayland);
	return NULL;
}

void wlr_xwayland_set_cursor(struct wlr_xwayland *wlr_xwayland,
		uint8_t *pixels, uint32_t stride, uint32_t width, uint32_t height,
		int32_t hotspot_x, int32_t hotspot_y) {
	if (wlr_xwayland->xwm != NULL) {
		xwm_set_cursor(wlr_xwayland->xwm, pixels, stride, width, height,
			hotspot_x, hotspot_y);
		return;
	}

	free(wlr_xwayland->cursor);

	wlr_xwayland->cursor = calloc(1, sizeof(struct wlr_xwayland_cursor));
	if (wlr_xwayland->cursor == NULL) {
		return;
	}
	wlr_xwayland->cursor->pixels = pixels;
	wlr_xwayland->cursor->stride = stride;
	wlr_xwayland->cursor->width = width;
	wlr_xwayland->cursor->height = height;
	wlr_xwayland->cursor->hotspot_x = hotspot_x;
	wlr_xwayland->cursor->hotspot_y = hotspot_y;
}

static void xwayland_handle_seat_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_xwayland *xwayland =
		wl_container_of(listener, xwayland, seat_destroy);

	wlr_xwayland_set_seat(xwayland, NULL);
}

void wlr_xwayland_set_seat(struct wlr_xwayland *xwayland,
		struct wlr_seat *seat) {
	if (xwayland->seat) {
		wl_list_remove(&xwayland->seat_destroy.link);
	}

	xwayland->seat = seat;

	if (xwayland->xwm) {
		xwm_set_seat(xwayland->xwm, seat);
	}

	if (seat == NULL) {
		return;
	}

	xwayland->seat_destroy.notify = xwayland_handle_seat_destroy;
	wl_signal_add(&seat->events.destroy, &xwayland->seat_destroy);
}
