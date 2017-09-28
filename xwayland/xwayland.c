#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <wayland-server.h>
#include "wlr/util/log.h"
#include "wlr/xwayland.h"
#include "sockets.h"
#include "xwm.h"

static void safe_close(int fd) {
	if (fd >= 0) {
		close(fd);
	}
}

static int unset_cloexec(int fd) {
	if (fcntl(fd, F_SETFD, 0) != 0) {
		wlr_log_errno(L_ERROR, "fcntl() failed on fd %d", fd);
		return -1;
	}
	return 0;
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

static void exec_xwayland(struct wlr_xwayland *wlr_xwayland) {
	if (unset_cloexec(wlr_xwayland->x_fd[0]) ||
			unset_cloexec(wlr_xwayland->x_fd[1]) ||
			unset_cloexec(wlr_xwayland->wm_fd[1]) ||
			unset_cloexec(wlr_xwayland->wl_fd[1])) {
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
		NULL };
	char **cur_arg = argv;

	if (fill_arg(&cur_arg, ":%d", wlr_xwayland->display) < 0 ||
			fill_arg(&cur_arg, "%d", wlr_xwayland->x_fd[0]) < 0 ||
			fill_arg(&cur_arg, "%d", wlr_xwayland->x_fd[1]) < 0 ||
			fill_arg(&cur_arg, "%d", wlr_xwayland->wm_fd[1]) < 0) {
		wlr_log_errno(L_ERROR, "alloc/print failure");
		_exit(EXIT_FAILURE);
	}

	const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
	if (!xdg_runtime) {
		wlr_log(L_ERROR, "XDG_RUNTIME_DIR is not set");
		_exit(EXIT_FAILURE);
	}

	if (clearenv()) {	    
		wlr_log_errno(L_ERROR, "clearenv failed");
		_exit(EXIT_FAILURE);
	}
	setenv("XDG_RUNTIME_DIR", xdg_runtime, true);
	char wayland_socket_str[16];
	snprintf(wayland_socket_str, sizeof(wayland_socket_str), "%d", wlr_xwayland->wl_fd[1]);
	setenv("WAYLAND_SOCKET", wayland_socket_str, true);

	wlr_log(L_INFO, "WAYLAND_SOCKET=%d Xwayland :%d -rootless -terminate -listen %d -listen %d -wm %d",
		wlr_xwayland->wl_fd[1], wlr_xwayland->display, wlr_xwayland->x_fd[0],
		wlr_xwayland->x_fd[1], wlr_xwayland->wm_fd[1]);

	// TODO: close stdout/err depending on log level

	execvp("Xwayland", argv);
}

static bool wlr_xwayland_init(struct wlr_xwayland *wlr_xwayland,
		struct wl_display *wl_display, struct wlr_compositor *compositor);
static void wlr_xwayland_finish(struct wlr_xwayland *wlr_xwayland);

static void xwayland_destroy_event(struct wl_listener *listener, void *data) {
	struct wlr_xwayland *wlr_xwayland = wl_container_of(listener, wlr_xwayland, destroy_listener);

	/* don't call client destroy */
	wlr_xwayland->client = NULL;
	wl_list_remove(&wlr_xwayland->destroy_listener.link);
	wlr_xwayland_finish(wlr_xwayland);

	if (time(NULL) - wlr_xwayland->server_start > 5) {
		wlr_xwayland_init(wlr_xwayland, wlr_xwayland->wl_display,
			wlr_xwayland->compositor);
	}
}

static void wlr_xwayland_finish(struct wlr_xwayland *wlr_xwayland) {
	if (!wlr_xwayland || wlr_xwayland->display == -1) {
		return;
	}
	if (wlr_xwayland->client) {
		wl_list_remove(&wlr_xwayland->destroy_listener.link);
		wl_client_destroy(wlr_xwayland->client);
	}
	if (wlr_xwayland->sigusr1_source) {
		wl_event_source_remove(wlr_xwayland->sigusr1_source);
	}

	xwm_destroy(wlr_xwayland->xwm);

	safe_close(wlr_xwayland->x_fd[0]);
	safe_close(wlr_xwayland->x_fd[1]);
	safe_close(wlr_xwayland->wl_fd[0]);
	safe_close(wlr_xwayland->wl_fd[1]);
	safe_close(wlr_xwayland->wm_fd[0]);
	safe_close(wlr_xwayland->wm_fd[1]);

	unlink_display_sockets(wlr_xwayland->display);
	wlr_xwayland->display = -1;
	unsetenv("DISPLAY");
	/* We do not kill the Xwayland process, it dies to broken pipe
	 * after we close our side of the wm/wl fds. This is more reliable
	 * than trying to kill something that might no longer be Xwayland.
	 */
}

static int xserver_handle_ready(int signal_number, void *data) {
	struct wlr_xwayland *wlr_xwayland = data;

	int stat_val = -1;
	while (waitpid(wlr_xwayland->pid, &stat_val, 0) < 0) {
		if (errno == EINTR) {
			continue;
		}
		wlr_log_errno(L_ERROR, "waitpid for Xwayland fork failed");
		return 1;
	}
	if (stat_val) {
		wlr_log(L_ERROR, "Xwayland startup failed, not setting up xwm");
		return 1;
	}
	wlr_log(L_DEBUG, "Xserver is ready");

	wlr_xwayland->xwm = xwm_create(wlr_xwayland);
	if (!wlr_xwayland->xwm) {
		wlr_xwayland_finish(wlr_xwayland);
		return 1;
	}

	wl_event_source_remove(wlr_xwayland->sigusr1_source);
	wlr_xwayland->sigusr1_source = NULL;

	char display_name[16];
	snprintf(display_name, sizeof(display_name), ":%d", wlr_xwayland->display);
	setenv("DISPLAY", display_name, true);

	return 1; /* wayland event loop dispatcher's count */
}

static bool wlr_xwayland_init(struct wlr_xwayland *wlr_xwayland,
		struct wl_display *wl_display, struct wlr_compositor *compositor) {
	memset(wlr_xwayland, 0, sizeof(struct wlr_xwayland));
	wlr_xwayland->wl_display = wl_display;
	wlr_xwayland->compositor = compositor;
	wlr_xwayland->x_fd[0] = wlr_xwayland->x_fd[1] = -1;
	wlr_xwayland->wl_fd[0] = wlr_xwayland->wl_fd[1] = -1;
	wlr_xwayland->wm_fd[0] = wlr_xwayland->wm_fd[1] = -1;
	wl_list_init(&wlr_xwayland->displayable_windows);
	wl_signal_init(&wlr_xwayland->events.new_surface);

	wlr_xwayland->display = open_display_sockets(wlr_xwayland->x_fd);
	if (wlr_xwayland->display < 0) {
		wlr_xwayland_finish(wlr_xwayland);
		return false;
	}
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, wlr_xwayland->wl_fd) != 0 ||
			socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, wlr_xwayland->wm_fd) != 0) {
		wlr_log_errno(L_ERROR, "failed to create socketpair");
		wlr_xwayland_finish(wlr_xwayland);
		return false;
	}

	wlr_xwayland->server_start = time(NULL);

	if (!(wlr_xwayland->client = wl_client_create(wl_display, wlr_xwayland->wl_fd[0]))) {
		wlr_log_errno(L_ERROR, "wl_client_create failed");
		wlr_xwayland_finish(wlr_xwayland);
		return false;
	}
	wlr_xwayland->wl_fd[0] = -1; /* not ours anymore */

	wlr_xwayland->destroy_listener.notify = xwayland_destroy_event;
	wl_client_add_destroy_listener(wlr_xwayland->client, &wlr_xwayland->destroy_listener);

	struct wl_event_loop *loop = wl_display_get_event_loop(wl_display);
	wlr_xwayland->sigusr1_source = wl_event_loop_add_signal(loop, SIGUSR1, xserver_handle_ready, wlr_xwayland);

	if ((wlr_xwayland->pid = fork()) == 0) {
		/* Double-fork, but we need to forward SIGUSR1 once Xserver(1)
		 * is ready, or error if there was one. */
		pid_t pid, ppid;
		sigset_t sigset;
		int sig;
		ppid = getppid();
		sigemptyset(&sigset);
		sigaddset(&sigset, SIGUSR1);
		sigaddset(&sigset, SIGCHLD);
		sigprocmask(SIG_BLOCK, &sigset, NULL);
		if ((pid = fork()) == 0) {
			exec_xwayland(wlr_xwayland);
			wlr_log_errno(L_ERROR, "failed to exec Xwayland");
			_exit(EXIT_FAILURE);
		}
		if (pid < 0) {
			wlr_log_errno(L_ERROR, "second fork failed");
			_exit(EXIT_FAILURE);
		}
		sigwait(&sigset, &sig);
		kill(ppid, SIGUSR1);
		wlr_log(L_DEBUG, "sent SIGUSR1 to process %d", ppid);
		if (sig == SIGCHLD) {
			waitpid(pid, NULL, 0);
			_exit(EXIT_FAILURE);
		}
		_exit(EXIT_SUCCESS);
	}
	if (wlr_xwayland->pid < 0) {
		wlr_log_errno(L_ERROR, "fork failed");
		wlr_xwayland_finish(wlr_xwayland);
		return false;
	}

	/* close child fds */
	close(wlr_xwayland->x_fd[0]);
	close(wlr_xwayland->x_fd[1]);
	close(wlr_xwayland->wl_fd[1]);
	close(wlr_xwayland->wm_fd[1]);
	wlr_xwayland->x_fd[0] = wlr_xwayland->x_fd[1] = -1;
	wlr_xwayland->wl_fd[1] = wlr_xwayland->wm_fd[1] = -1;

	return true;
}

void wlr_xwayland_destroy(struct wlr_xwayland *wlr_xwayland) {
	wlr_xwayland_finish(wlr_xwayland);
	free(wlr_xwayland);
}

struct wlr_xwayland *wlr_xwayland_create(struct wl_display *wl_display,
		struct wlr_compositor *compositor) {
	struct wlr_xwayland *wlr_xwayland = calloc(1, sizeof(struct wlr_xwayland));
	if (wlr_xwayland_init(wlr_xwayland, wl_display, compositor)) {
		return wlr_xwayland;
	}
	free(wlr_xwayland);
	return NULL;
}
