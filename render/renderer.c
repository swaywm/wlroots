#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <GLES3/gl3.h>
#include <wayland-util.h>
#include <wayland-server-protocol.h>
#include <wlr/render.h>
#include <wlr/render/matrix.h>
#include <common/log.h>
#include "render.h"

static struct wlr_shader *default_shader = NULL;

static const GLchar vert_src[] =
"uniform mat4 proj;\n"
"attribute vec2 pos;\n"
"attribute vec2 texcoord;\n"
"varying vec2 v_texcoord;\n"
"void main() {\n"
"	gl_Position = proj * vec4(pos, 0.0, 1.0);\n"
"	v_texcoord = texcoord;\n"
"}\n";

static const GLchar frag_src_RGB[] =
"precision mediump float;\n"
"varying vec2 v_texcoord;\n"
"uniform sampler2D tex;\n"
"void main() {\n"
"	gl_FragColor = vec4(texture2D(tex, v_texcoord).rgb, 1.0);\n"
"}\n";

static const GLchar frag_src_RGBA[] =
"precision mediump float;\n"
"varying vec2 v_texcoord;\n"
"uniform sampler2D tex;\n"
"void main() {\n"
"	gl_FragColor = texture2D(tex, v_texcoord);\n"
"}\n";

static void default_shader_init() {
	if (default_shader) {
		return;
	}
	default_shader = wlr_shader_init(vert_src);
	if (!default_shader) {
		goto error;
	}
	if (!wlr_shader_add_format(default_shader, GL_RGB, frag_src_RGB)) {
		goto error;
	}
	if (!wlr_shader_add_format(default_shader, GL_RGBA, frag_src_RGBA)) {
		goto error;
	}
	return;
error:
	wlr_shader_destroy(default_shader);
	wlr_log(L_ERROR, "Failed to set up default shaders!");
}

static GLuint vao, vbo, ebo;

static void default_quad_init() {
	GLfloat verticies[] = {
		1, 1, 1, 1, // bottom right
		1, 0, 1, 0, // top right
		0, 0, 0, 0, // top left
		0, 1, 0, 1, // bottom left
	};
	GLuint indicies[] = {
		0, 1, 3,
		1, 2, 3,
	};

	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
	glBufferData(GL_ARRAY_BUFFER, sizeof(verticies), verticies, GL_STATIC_DRAW);

	glGenBuffers(1, &ebo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indicies), indicies, GL_STATIC_DRAW);
}

static void global_init() {
	default_shader_init();
	default_quad_init();
}

struct wlr_renderer *wlr_renderer_init() {
	global_init();
	struct wlr_renderer *r = calloc(sizeof(struct wlr_renderer), 1);
	r->shader = default_shader;
	return r;
}

void wlr_renderer_set_shader(struct wlr_renderer *renderer,
		struct wlr_shader *shader) {
	assert(renderer);
	renderer->shader = shader;
}

bool wlr_render_quad(struct wlr_renderer *renderer,
		struct wlr_surface *surf, float (*transform)[16],
		float x, float y) {
	assert(renderer && surf && renderer->shader && surf->valid);
	wlr_shader_use(renderer->shader, surf->format);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glActiveTexture(GL_TEXTURE0 + 1);
	glBindTexture(GL_TEXTURE_2D, surf->tex_id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	float world[16], final[16];
	wlr_matrix_identity(&final);
	wlr_matrix_translate(&world, x, y, 0);
	wlr_matrix_mul(&final, &world, &final);
	wlr_matrix_scale(&world, surf->width, surf->height, 1);
	wlr_matrix_mul(&final, &world, &final);
	wlr_matrix_mul(transform, &final, &final);
	glUniformMatrix4fv(0, 1, GL_TRUE, final);

	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
	return false;
}

void wlr_renderer_destroy(struct wlr_renderer *renderer) {
	// TODO
}
