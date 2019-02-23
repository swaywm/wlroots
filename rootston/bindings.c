#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <wlr/util/log.h>
#include "rootston/bindings.h"
#include "rootston/view.h"

static bool outputs_enabled = true;

static const char exec_prefix[] = "exec ";

static void double_fork_shell_cmd(const char *shell_cmd) {
	pid_t pid = fork();
	if (pid < 0) {
		wlr_log(WLR_ERROR, "cannot execute binding command: fork() failed");
		return;
	}

	if (pid == 0) {
		pid = fork();
		if (pid == 0) {
			execl("/bin/sh", "/bin/sh", "-c", shell_cmd, NULL);
			_exit(EXIT_FAILURE);
		} else {
			_exit(pid == -1);
		}
	}

	int status;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno == EINTR) {
			continue;
		}
		wlr_log_errno(WLR_ERROR, "waitpid() on first child failed");
		return;
	}

	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		return;
	}

	wlr_log(WLR_ERROR, "first child failed to fork command");
}

void execute_binding_command(struct roots_seat *seat,
		struct roots_input *input, const char *command) {
	if (strcmp(command, "exit") == 0) {
		wl_display_terminate(input->server->wl_display);
	} else if (strcmp(command, "close") == 0) {
		struct roots_view *focus = roots_seat_get_focus(seat);
		if (focus != NULL) {
			view_close(focus);
		}
	} else if (strcmp(command, "fullscreen") == 0) {
		struct roots_view *focus = roots_seat_get_focus(seat);
		if (focus != NULL) {
			bool is_fullscreen = focus->fullscreen_output != NULL;
			view_set_fullscreen(focus, !is_fullscreen, NULL);
		}
	} else if (strcmp(command, "next_window") == 0) {
		roots_seat_cycle_focus(seat);
	} else if (strcmp(command, "alpha") == 0) {
		struct roots_view *focus = roots_seat_get_focus(seat);
		if (focus != NULL) {
			view_cycle_alpha(focus);
		}
	} else if (strncmp(exec_prefix, command, strlen(exec_prefix)) == 0) {
		const char *shell_cmd = command + strlen(exec_prefix);
		double_fork_shell_cmd(shell_cmd);
	} else if (strcmp(command, "maximize") == 0) {
		struct roots_view *focus = roots_seat_get_focus(seat);
		if (focus != NULL) {
			view_maximize(focus, !focus->maximized);
		}
	} else if (strcmp(command, "nop") == 0) {
		wlr_log(WLR_DEBUG, "nop command");
	} else if (strcmp(command, "toggle_outputs") == 0) {
		outputs_enabled = !outputs_enabled;
		struct roots_output *output;
		wl_list_for_each(output, &input->server->desktop->outputs, link) {
			wlr_output_enable(output->wlr_output, outputs_enabled);
		}
	} else if (strcmp(command, "toggle_decoration_mode") == 0) {
		struct roots_view *focus = roots_seat_get_focus(seat);
		if (focus != NULL && focus->type == ROOTS_XDG_SHELL_VIEW) {
			struct roots_xdg_surface *xdg_surface =
				roots_xdg_surface_from_view(focus);
			struct roots_xdg_toplevel_decoration *decoration =
				xdg_surface->xdg_toplevel_decoration;
			if (decoration != NULL) {
				enum wlr_xdg_toplevel_decoration_v1_mode mode =
					decoration->wlr_decoration->current_mode;
				mode = mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
					? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
					: WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
				wlr_xdg_toplevel_decoration_v1_set_mode(
					decoration->wlr_decoration, mode);
			}
		}
	} else if (strcmp(command, "break_pointer_constraint") == 0) {
		struct wl_list *list = &input->seats;
		struct roots_seat *seat;
		wl_list_for_each(seat, list, link) {
			roots_cursor_constrain(seat->cursor, NULL, NAN, NAN);
		}
	} else {
		wlr_log(WLR_ERROR, "unknown binding command: %s", command);
	}
}
