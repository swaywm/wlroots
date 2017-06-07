#define _POSIX_C_SOURCE 199309L
#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wayland-server-protocol.h>
#include <GLES3/gl3.h>
#include <wlr/backend.h>
#include <wlr/session.h>
#include <wlr/types.h>
#include <math.h>
#include "cat.h"

static const GLchar vert_src[] =
"#version 310 es\n"
"precision mediump float;\n"
"layout(location = 0) uniform mat4 transform;\n"
"layout(location = 0) in vec2 pos;\n"
"layout(location = 1) in vec2 texcoord;\n"
"out vec2 v_texcoord;\n"
"void main() {\n"
"	v_texcoord = texcoord;\n"
"	gl_Position = transform * vec4(pos, 0.0, 1.0);\n"
"}\n";

static const GLchar frag_src[] =
"#version 310 es\n"
"precision mediump float;\n"
"in vec2 v_texcoord;\n"
"out vec4 color;\n"
"uniform sampler2D texture0;\n"
"void main() {\n"
"	color = texture(texture0, v_texcoord);\n"
"}\n";

struct state {
	struct timespec last_frame;
	struct wl_listener output_add;
	struct wl_listener output_remove;
	struct wl_list outputs;
	struct wl_list config;

	struct gl {
		GLuint prog;
		GLuint vao;
		GLuint vbo;
		GLuint ebo;
		GLuint tex;
	} gl;
};

struct output_state {
	struct wl_list link;
	struct wlr_output *output;
	struct state *state;
	struct wl_listener frame;
};

struct output_config {
	char *name;
	enum wl_output_transform transform;

	struct wl_list link;
};

static GLuint create_shader(GLenum type, const GLchar *src, GLint len) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, &len);
	glCompileShader(shader);

	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

	if (success == GL_FALSE) {
		GLint loglen;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &loglen);

		GLchar msg[loglen];
		glGetShaderInfoLog(shader, loglen, &loglen, msg);

		fprintf(stderr, "Failed to create shader: %s\n", msg);
		exit(1);
	}

	return shader;
}

static void init_gl(struct gl *gl) {
	GLuint vert = create_shader(GL_VERTEX_SHADER, vert_src, strlen(vert_src));
	GLuint frag = create_shader(GL_FRAGMENT_SHADER, frag_src, strlen(frag_src));

	gl->prog = glCreateProgram();
	glAttachShader(gl->prog, vert);
	glAttachShader(gl->prog, frag);
	glLinkProgram(gl->prog);

	GLint success;
	glGetProgramiv(gl->prog, GL_LINK_STATUS, &success);

	if (success == GL_FALSE) {
		GLint len;
		glGetProgramiv(gl->prog, GL_INFO_LOG_LENGTH, &len);

		GLchar msg[len];
		glGetProgramInfoLog(gl->prog, len, &len, msg);

		fprintf(stderr, "Failed to link program: %s\n", msg);
		exit(1);
	}

	glDeleteProgram(vert);
	glDeleteProgram(frag);

	GLfloat verticies[] = {
		1, 1, 1, 1, // bottom right
		1, 0, 1, 0, // top right
		0, 0, 0, 0, // top left
		0, 1, 0, 1, // bottom left
	};
	// Temporary
	for (size_t i = 0; i < sizeof(verticies) / sizeof(verticies[0]); i += 4) {
		verticies[i] *= 128.0f;
		verticies[i + 1] *= 128.0f;
		verticies[i] += 128;
		verticies[i + 1] += 128;
	}
	// End temp
	GLuint indicies[] = {
		0, 1, 3,
		1, 2, 3,
	};

	glGenVertexArrays(1, &gl->vao);
	glGenBuffers(1, &gl->vbo);

	glBindVertexArray(gl->vao);
	glBindBuffer(GL_ARRAY_BUFFER, gl->vbo);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
	glBufferData(GL_ARRAY_BUFFER, sizeof(verticies), verticies, GL_STATIC_DRAW);

	glGenBuffers(1, &gl->ebo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl->ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indicies), indicies, GL_STATIC_DRAW);

	glGenTextures(1, &gl->tex);
	glBindTexture(GL_TEXTURE_2D, gl->tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, cat_tex.width, cat_tex.height, 
			0, GL_RGB, GL_UNSIGNED_BYTE, cat_tex.pixel_data);
}

static void cleanup_gl(struct gl *gl) {
	glDeleteProgram(gl->prog);
	glDeleteVertexArrays(1, &gl->vao);
	glDeleteBuffers(1, &gl->vbo);
	glDeleteTextures(1, &gl->tex);
}

static void output_frame(struct wl_listener *listener, void *data) {
	struct output_state *ostate = wl_container_of(listener, ostate, frame);
	struct state *s = ostate->state;

	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);

	int32_t width = ostate->output->width;
	int32_t height = ostate->output->height;
	glViewport(0, 0, width, height);

	glUseProgram(s->gl.prog);

	glBindVertexArray(s->gl.vao);
	glBindBuffer(GL_ARRAY_BUFFER, s->gl.vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->gl.ebo);
	glActiveTexture(GL_TEXTURE0 + 1);
	glBindTexture(GL_TEXTURE_2D, s->gl.tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glUniformMatrix4fv(0, 1, GL_TRUE, ostate->output->transform_matrix);

	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	s->last_frame = now;
}

static void output_add(struct wl_listener *listener, void *data) {
	struct wlr_output *output = data;
	struct state *state = wl_container_of(listener, state, output_add);

	fprintf(stderr, "Output '%s' added\n", output->name);
	wlr_output_set_mode(output, output->modes->items[0]);

	struct output_state *ostate = calloc(1, sizeof(struct output_state));

	ostate->output = output;
	ostate->state = state;
	ostate->frame.notify = output_frame;

	struct output_config *conf;
	wl_list_for_each(conf, &state->config, link) {
		if (strcmp(conf->name, output->name) == 0) {
			wlr_output_transform(ostate->output, conf->transform);
			break;
		}
	}

	wl_list_init(&ostate->frame.link);
	wl_signal_add(&output->events.frame, &ostate->frame);
	wl_list_insert(&state->outputs, &ostate->link);
}

static void output_remove(struct wl_listener *listener, void *data) {
	struct wlr_output *output = data;
	struct state *state = wl_container_of(listener, state, output_remove);
	struct output_state *ostate;

	wl_list_for_each(ostate, &state->outputs, link) {
		if (ostate->output == output) {
			wl_list_remove(&ostate->link);
			wl_list_remove(&ostate->frame.link);
			free(ostate);
			break;
		}
	}
}

static int timer_done(void *data) {
	*(bool *)data = true;
	return 1;
}

static void usage(const char *name, int ret) {
	fprintf(stderr,
		"usage: %s [-d <name> [-r <rotation> | -f]]*\n"
		"\n"
		" -o <output>    The name of the DRM display. e.g. DVI-I-1.\n"
		" -r <rotation>  The rotation counter clockwise. Valid values are 90, 180, 270.\n"
		" -f             Flip the output along the vertical axis.\n", name);

	exit(ret);
}

static void parse_args(int argc, char *argv[], struct wl_list *config) {
	struct output_config *oc = NULL;

	int c;
	while ((c = getopt(argc, argv, "o:r:fh")) != -1) {
		switch (c) {
		case 'o':
			oc = calloc(1, sizeof(*oc));
			oc->name = optarg;
			oc->transform = WL_OUTPUT_TRANSFORM_NORMAL;
			wl_list_insert(config, &oc->link);
			break;
		case 'r':
			if (!oc) {
				fprintf(stderr, "You must specify an output first\n");
				usage(argv[0], 1);
			}

			if (oc->transform != WL_OUTPUT_TRANSFORM_NORMAL
					&& oc->transform != WL_OUTPUT_TRANSFORM_FLIPPED) {
				fprintf(stderr, "Rotation for %s already specified\n", oc->name);
				usage(argv[0], 1);
			}

			if (strcmp(optarg, "90") == 0) {
				oc->transform += WL_OUTPUT_TRANSFORM_90;
			} else if (strcmp(optarg, "180") == 0) {
				oc->transform += WL_OUTPUT_TRANSFORM_180;
			} else if (strcmp(optarg, "270") == 0) {
				oc->transform += WL_OUTPUT_TRANSFORM_270;
			} else {
				fprintf(stderr, "Invalid rotation '%s'\n", optarg);
				usage(argv[0], 1);
			}
			break;
		case 'f':
			if (!oc) {
				fprintf(stderr, "You must specify an output first\n");
				usage(argv[0], 1);
			}

			if (oc->transform >= WL_OUTPUT_TRANSFORM_FLIPPED) {
				fprintf(stderr, "Flip for %s already specified\n", oc->name);
				usage(argv[0], 1);
			}

			oc->transform += WL_OUTPUT_TRANSFORM_FLIPPED;
			break;
		case 'h':
		case '?':
			usage(argv[0], c != 'h');
		}
	}
}

int main(int argc, char *argv[]) {
	struct state state = {
		.output_add = { .notify = output_add },
		.output_remove = { .notify = output_remove }
	};

	wl_list_init(&state.outputs);
	wl_list_init(&state.config);
	wl_list_init(&state.output_add.link);
	wl_list_init(&state.output_remove.link);
	clock_gettime(CLOCK_MONOTONIC, &state.last_frame);

	parse_args(argc, argv, &state.config);

	struct wl_display *display = wl_display_create();
	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);

	struct wlr_session *session = wlr_session_start(display);
	if (!session) {
		return 1;
	}

	struct wlr_backend *wlr = wlr_backend_autocreate(display, session);
	if (!wlr) {
		return 1;
	}

	wl_signal_add(&wlr->events.output_add, &state.output_add);
	wl_signal_add(&wlr->events.output_remove, &state.output_remove);

	if (!wlr_backend_init(wlr)) {
		return 1;
	}

	init_gl(&state.gl);

	bool done = false;
	struct wl_event_source *timer = wl_event_loop_add_timer(event_loop,
		timer_done, &done);

	wl_event_source_timer_update(timer, 10000);

	while (!done) {
		wl_event_loop_dispatch(event_loop, 0);
	}

	cleanup_gl(&state.gl);

	wl_event_source_remove(timer);
	wlr_backend_destroy(wlr);
	wlr_session_finish(session);
	wl_display_destroy(display);

	struct output_config *ptr, *tmp;
	wl_list_for_each_safe(ptr, tmp, &state.config, link) {
		free(ptr);
	}
}
