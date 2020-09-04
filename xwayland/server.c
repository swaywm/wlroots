#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <stdnoreturn.h>
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

noreturn static void exec_xwayland(struct wlr_xwayland_server *server) {
	if (!set_cloexec(server->x_fd[0], false) ||
			!set_cloexec(server->x_fd[1], false) ||
			!set_cloexec(server->wl_fd[1], false)) {
		wlr_log(WLR_ERROR, "Failed to unset CLOEXEC on FD");
		_exit(EXIT_FAILURE);
	}
	if (server->enable_wm && !set_cloexec(server->wm_fd[1], false)) {
		wlr_log(WLR_ERROR, "Failed to unset CLOEXEC on FD");
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

	if (fill_arg(&cur_arg, ":%d", server->display) < 0 ||
			fill_arg(&cur_arg, "%d", server->x_fd[0]) < 0 ||
			fill_arg(&cur_arg, "%d", server->x_fd[1]) < 0) {
		wlr_log_errno(WLR_ERROR, "alloc/print failure");
		_exit(EXIT_FAILURE);
	}
	if (server->enable_wm) {
		if (fill_arg(&cur_arg, "%d", server->wm_fd[1]) < 0) {
			wlr_log_errno(WLR_ERROR, "alloc/print failure");
			_exit(EXIT_FAILURE);
		}
	} else {
		cur_arg++;
		*cur_arg = NULL;
	}

	char wayland_socket_str[16];
	snprintf(wayland_socket_str, sizeof(wayland_socket_str), "%d", server->wl_fd[1]);
	setenv("WAYLAND_SOCKET", wayland_socket_str, true);

	wlr_log(WLR_INFO, "WAYLAND_SOCKET=%d Xwayland :%d -rootless -terminate -listen %d -listen %d -wm %d",
		server->wl_fd[1], server->display, server->x_fd[0],
		server->x_fd[1], server->wm_fd[1]);

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

static void server_finish_process(struct wlr_xwayland_server *server) {
	if (!server || server->display == -1) {
		return;
	}

	if (server->x_fd_read_event[0]) {
		wl_event_source_remove(server->x_fd_read_event[0]);
		wl_event_source_remove(server->x_fd_read_event[1]);

		server->x_fd_read_event[0] = server->x_fd_read_event[1] = NULL;
	}

	if (server->client) {
		wl_list_remove(&server->client_destroy.link);
		wl_client_destroy(server->client);
	}
	if (server->sigusr1_source) {
		wl_event_source_remove(server->sigusr1_source);
	}

	safe_close(server->wl_fd[0]);
	safe_close(server->wl_fd[1]);
	safe_close(server->wm_fd[0]);
	safe_close(server->wm_fd[1]);
	memset(server, 0, offsetof(struct wlr_xwayland_server, display));
	server->wl_fd[0] = server->wl_fd[1] = -1;
	server->wm_fd[0] = server->wm_fd[1] = -1;

	/* We do not kill the Xwayland process, it dies to broken pipe
	 * after we close our side of the wm/wl fds. This is more reliable
	 * than trying to kill something that might no longer be Xwayland.
	 */
}

static void server_finish_display(struct wlr_xwayland_server *server) {
	if (!server) {
		return;
	}

	wl_list_remove(&server->display_destroy.link);

	if (server->display == -1) {
		return;
	}

	safe_close(server->x_fd[0]);
	safe_close(server->x_fd[1]);
	server->x_fd[0] = server->x_fd[1] = -1;

	unlink_display_sockets(server->display);
	server->display = -1;
	server->display_name[0] = '\0';
}

static bool server_start(struct wlr_xwayland_server *server);
static bool server_start_lazy(struct wlr_xwayland_server *server);

static void handle_client_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xwayland_server *server =
		wl_container_of(listener, server, client_destroy);

	if (server->sigusr1_source) {
		// Xwayland failed to start, let the sigusr1 handler deal with it
		return;
	}

	// Don't call client destroy: it's being destroyed already
	server->client = NULL;
	wl_list_remove(&server->client_destroy.link);

	server_finish_process(server);

	if (time(NULL) - server->server_start > 5) {
		if (server->lazy) {
			wlr_log(WLR_INFO, "Restarting Xwayland (lazy)");
			server_start_lazy(server);
		} else  {
			wlr_log(WLR_INFO, "Restarting Xwayland");
			server_start(server);
		}
	}
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xwayland_server *server =
		wl_container_of(listener, server, display_destroy);

	// Don't call client destroy: the display is being destroyed, it's too late
	if (server->client) {
		server->client = NULL;
		wl_list_remove(&server->client_destroy.link);
	}

	wlr_xwayland_server_destroy(server);
}

static int xserver_handle_ready(int signal_number, void *data) {
	struct wlr_xwayland_server *server = data;

	int stat_val = -1;
	while (waitpid(server->pid, &stat_val, 0) < 0) {
		if (errno == EINTR) {
			continue;
		}
		wlr_log_errno(WLR_ERROR, "waitpid for Xwayland fork failed");
		goto error;
	}
	if (stat_val) {
		wlr_log(WLR_ERROR, "Xwayland startup failed, not setting up xwm");
		goto error;
	}
	wlr_log(WLR_DEBUG, "Xserver is ready");

	wl_event_source_remove(server->sigusr1_source);
	server->sigusr1_source = NULL;

	struct wlr_xwayland_server_ready_event event = {
		.server = server,
		.wm_fd = server->wm_fd[0],
	};
	wlr_signal_emit_safe(&server->events.ready, &event);

	return 1; /* wayland event loop dispatcher's count */

error:
	/* clean up */
	server_finish_process(server);
	server_finish_display(server);
	return 1;
}

static bool server_start_display(struct wlr_xwayland_server *server,
		struct wl_display *wl_display) {
	server->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(wl_display, &server->display_destroy);

	server->display = open_display_sockets(server->x_fd);
	if (server->display < 0) {
		server_finish_display(server);
		return false;
	}

	snprintf(server->display_name, sizeof(server->display_name),
		":%d", server->display);
	return true;
}

static bool server_start(struct wlr_xwayland_server *server) {
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, server->wl_fd) != 0) {
		wlr_log_errno(WLR_ERROR, "socketpair failed");
		server_finish_process(server);
		return false;
	}
	if (!set_cloexec(server->wl_fd[0], true) ||
			!set_cloexec(server->wl_fd[1], true)) {
		wlr_log(WLR_ERROR, "Failed to set O_CLOEXEC on socket");
		server_finish_process(server);
		return false;
	}
	if (server->enable_wm) {
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, server->wm_fd) != 0) {
			wlr_log_errno(WLR_ERROR, "socketpair failed");
			server_finish_process(server);
			return false;
		}
		if (!set_cloexec(server->wm_fd[0], true) ||
				!set_cloexec(server->wm_fd[1], true)) {
			wlr_log(WLR_ERROR, "Failed to set O_CLOEXEC on socket");
			server_finish_process(server);
			return false;
		}
	}

	server->server_start = time(NULL);

	server->client = wl_client_create(server->wl_display, server->wl_fd[0]);
	if (!server->client) {
		wlr_log_errno(WLR_ERROR, "wl_client_create failed");
		server_finish_process(server);
		return false;
	}

	server->wl_fd[0] = -1; /* not ours anymore */

	server->client_destroy.notify = handle_client_destroy;
	wl_client_add_destroy_listener(server->client, &server->client_destroy);

	struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
	server->sigusr1_source = wl_event_loop_add_signal(loop, SIGUSR1,
		xserver_handle_ready, server);

	server->pid = fork();
	if (server->pid < 0) {
		wlr_log_errno(WLR_ERROR, "fork failed");
		server_finish_process(server);
		return false;
	} else if (server->pid == 0) {
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
			exec_xwayland(server);
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
	close(server->wl_fd[1]);
	safe_close(server->wm_fd[1]);
	server->wl_fd[1] = server->wm_fd[1] = -1;

	return true;
}

static int xwayland_socket_connected(int fd, uint32_t mask, void *data) {
	struct wlr_xwayland_server *server = data;

	wl_event_source_remove(server->x_fd_read_event[0]);
	wl_event_source_remove(server->x_fd_read_event[1]);
	server->x_fd_read_event[0] = server->x_fd_read_event[1] = NULL;

	server_start(server);

	return 0;
}

static bool server_start_lazy(struct wlr_xwayland_server *server) {
	struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);

	if (!(server->x_fd_read_event[0] = wl_event_loop_add_fd(loop, server->x_fd[0],
				WL_EVENT_READABLE, xwayland_socket_connected, server))) {
		return false;
	}

	if (!(server->x_fd_read_event[1] = wl_event_loop_add_fd(loop, server->x_fd[1],
				WL_EVENT_READABLE, xwayland_socket_connected, server))) {
		wl_event_source_remove(server->x_fd_read_event[0]);
		server->x_fd_read_event[0] = NULL;
		return false;
	}

	return true;
}

void wlr_xwayland_server_destroy(struct wlr_xwayland_server *server) {
	if (!server) {
		return;
	}

	server_finish_process(server);
	server_finish_display(server);
	wlr_signal_emit_safe(&server->events.destroy, NULL);
	free(server);
}

struct wlr_xwayland_server *wlr_xwayland_server_create(
		struct wl_display *wl_display,
		struct wlr_xwayland_server_options *options) {
	struct wlr_xwayland_server *server =
		calloc(1, sizeof(struct wlr_xwayland_server));
	if (!server) {
		return NULL;
	}

	server->wl_display = wl_display;
	server->lazy = options->lazy;
	server->enable_wm = options->enable_wm;

	server->x_fd[0] = server->x_fd[1] = -1;
	server->wl_fd[0] = server->wl_fd[1] = -1;
	server->wm_fd[0] = server->wm_fd[1] = -1;

	wl_signal_init(&server->events.ready);
	wl_signal_init(&server->events.destroy);

	if (!server_start_display(server, wl_display)) {
		goto error_alloc;
	}

	if (server->lazy) {
		if (!server_start_lazy(server)) {
			goto error_display;
		}
	} else {
		if (!server_start(server)) {
			goto error_display;
		}
	}

	return server;

error_display:
	server_finish_display(server);
error_alloc:
	free(server);
	return NULL;
}
