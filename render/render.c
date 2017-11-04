#define _XOPEN_SOURCE // Exposes M_PI

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include "render/render.h"
#include "render/glapi.h"

/*
 * The wayland formats are little endian while the GL formats are big endian,
 * so WL_SHM_FORMAT_ARGB8888 is actually compatible with GL_BGRA_EXT.
 */

static const struct format formats[] = {
	{
		.wl_fmt = WL_SHM_FORMAT_ARGB8888,
		.gl_fmt = GL_BGRA_EXT,
		.gl_type = GL_UNSIGNED_BYTE,
		.bpp = 32,
	},
	{
		.wl_fmt = WL_SHM_FORMAT_XRGB8888,
		.gl_fmt = GL_BGRA_EXT,
		.gl_type = GL_UNSIGNED_BYTE,
		.bpp = 32,
	},
	{
		.wl_fmt = WL_SHM_FORMAT_ABGR8888,
		.gl_fmt = GL_RGBA,
		.gl_type = GL_UNSIGNED_BYTE,
		.bpp = 32,
	},
	{
		.wl_fmt = WL_SHM_FORMAT_XBGR8888,
		.gl_fmt = GL_RGBA,
		.gl_type = GL_UNSIGNED_BYTE,
		.bpp = 32,
	},
};

const struct format *wl_to_gl(enum wl_shm_format fmt) {
	for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
		if (formats[i].wl_fmt == fmt) {
			return &formats[i];
		}
	}

	return NULL;
}

bool wlr_render_format_supported(enum wl_shm_format wl_fmt) {
	return wl_to_gl(wl_fmt);
}

static const float transforms[][4] = {
	[WL_OUTPUT_TRANSFORM_NORMAL] = {
		1.0f, 0.0f,
		0.0f, -1.0f,
	},
	[WL_OUTPUT_TRANSFORM_90] = {
		0.0f, -1.0f,
		-1.0f, 0.0f,
	},
	[WL_OUTPUT_TRANSFORM_180] = {
		-1.0f, 0.0f,
		0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_270] = {
		0.0f, 1.0f,
		1.0f, 0.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED] = {
		-1.0f, 0.0f,
		0.0f, -1.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_90] = {
		0.0f, 1.0f,
		-1.0f, 0.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_180] = {
		1.0f, 0.0f,
		0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_270] = {
		0.0f, -1.0f,
		1.0f, 0.0f,
	},
};

// Equivilent to glOrtho(0, width, 0, height, 1, -1) with the transform applied
static void matrix(float mat[static 9], int32_t width, int32_t height,
		enum wl_output_transform transform) {
	memset(mat, 0, sizeof(*mat) * 9);

	const float *t = transforms[transform];
	float x = 2.0f / width;
	float y = 2.0f / height;

	// Rotation + relection
	mat[0] = x * t[0];
	mat[1] = x * t[1];
	mat[3] = y * t[2];
	mat[4] = y * t[3];

	// Translation
	mat[2] = -copysign(1.0f, mat[0] + mat[1]);
	mat[5] = -copysign(1.0f, mat[3] + mat[4]);

	// Identity
	mat[8] = 1.0f;
}

void wlr_render_bind_raw(struct wlr_render *rend, uint32_t width, uint32_t height,
		enum wl_output_transform transform) {
	assert(eglGetCurrentContext() == rend->egl->context);
	DEBUG_PUSH;

	glViewport(0, 0, width, height);
	matrix(rend->proj, width, height, transform);

	DEBUG_POP;
}

void wlr_render_bind(struct wlr_render *rend, struct wlr_output *output) {
	wlr_render_bind_raw(rend, output->width, output->height, output->transform);
}

void wlr_render_clear(struct wlr_render *rend, float r, float g, float b, float a) {
	assert(eglGetCurrentContext() == rend->egl->context);
	DEBUG_PUSH;

	glClearColor(r, g, b, a);
	glClear(GL_COLOR_BUFFER_BIT);

	DEBUG_POP;
}

void wlr_render_subtexture(struct wlr_render *rend, struct wlr_tex *tex,
		int32_t tex_x1, int32_t tex_y1, int32_t tex_x2, int32_t tex_y2,
		int32_t pos_x1, int32_t pos_y1, int32_t pos_x2, int32_t pos_y2, int32_t pos_z) {
	assert(eglGetCurrentContext() == rend->egl->context);
	DEBUG_PUSH;

	GLuint prog = rend->shaders.tex;

	GLuint proj_loc = glGetUniformLocation(prog, "proj");
	GLuint pos_loc = glGetAttribLocation(prog, "pos");
	GLuint texcoord_loc = glGetAttribLocation(prog, "texcoord");

	glUseProgram(prog);
	glUniformMatrix3fv(proj_loc, 1, GL_TRUE, rend->proj);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex->image_tex);

	GLfloat verts[] = {
		pos_x1, pos_y1, pos_z,
		pos_x2, pos_y1, pos_z,
		pos_x1, pos_y2, pos_z,
		pos_x2, pos_y2, pos_z,
	};

	GLfloat tw = tex->width;
	GLfloat th = tex->height;
	// Transform to OpenGL [0,1] texture coordinate space
	GLfloat tx1 = tex_x1 / tw;
	GLfloat tx2 = tex_x2 / tw;
	GLfloat ty1 = tex_y1 / th;
	GLfloat ty2 = tex_y2 / th;

	GLfloat texcoord[] = {
		tx1, ty1,
		tx2, ty1,
		tx1, ty2,
		tx2, ty2,
	};

	glVertexAttribPointer(pos_loc, 3, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(texcoord_loc, 2, GL_FLOAT, GL_FALSE, 0, texcoord);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glEnableVertexAttribArray(pos_loc);
	glEnableVertexAttribArray(texcoord_loc);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	DEBUG_POP;
}

void wlr_render_texture(struct wlr_render *rend, struct wlr_tex *tex,
		int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t z) {
	wlr_render_subtexture(rend, tex, 0, 0, tex->width, tex->height, x1, y1, x2, y2, z);
}

void wlr_render_rect(struct wlr_render *rend, float r, float g, float b, float a,
		int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t z) {
	assert(eglGetCurrentContext() == rend->egl->context);
	DEBUG_PUSH;

	GLuint prog = rend->shaders.poly;

	GLuint proj_loc = glGetUniformLocation(prog, "proj");
	GLuint pos_loc = glGetAttribLocation(prog, "pos");
	GLuint color_loc = glGetUniformLocation(prog, "color");

	glUseProgram(prog);
	glUniformMatrix3fv(proj_loc, 1, GL_TRUE, rend->proj);
	glUniform4f(color_loc, r, g, b, a);

	GLfloat verts[] = {
		x1, y1, z,
		x2, y1, z,
		x1, y2, z,
		x2, y2, z,
	};

	glVertexAttribPointer(pos_loc, 3, GL_FLOAT, GL_FALSE, 0, verts);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glEnableVertexAttribArray(pos_loc);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	DEBUG_POP;
}

void wlr_render_ellipse(struct wlr_render *rend, float r, float g, float b, float a,
		int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t z) {
	assert(eglGetCurrentContext() == rend->egl->context);
	DEBUG_PUSH;

	GLuint prog = rend->shaders.poly;

	GLuint proj_loc = glGetUniformLocation(prog, "proj");
	GLuint pos_loc = glGetAttribLocation(prog, "pos");
	GLuint color_loc = glGetUniformLocation(prog, "color");

	glUseProgram(prog);
	glUniformMatrix3fv(proj_loc, 1, GL_TRUE, rend->proj);
	glUniform4f(color_loc, r, g, b, a);

	float x = (x1 + x2) / 2.0f;
	float y = (y1 + y2) / 2.0f;
	float rw = abs(x1 - x2) / 2.0f;
	float rh = abs(y1 - y2) / 2.0f;

	GLfloat verts[18 * 3] = {
		x, y, z,
	};

	for (int i = 0; i < 17; ++i) {
		float angle = M_PI / 8.0f * i;
		int base = (i + 1) * 3;
		verts[base + 0] = x + sin(angle) * rw;
		verts[base + 1] = y + cos(angle) * rh;
		verts[base + 2] = z;
	}

	glVertexAttribPointer(pos_loc, 3, GL_FLOAT, GL_FALSE, 0, verts);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glEnableVertexAttribArray(pos_loc);

	glDrawArrays(GL_TRIANGLE_FAN, 0, 18);

	DEBUG_POP;
}

bool wlr_render_read_pixels(struct wlr_render *rend, enum wl_shm_format wl_fmt,
		uint32_t stride, uint32_t width, uint32_t height,
		uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y,
		void *data) {
	assert(eglGetCurrentContext() == rend->egl->context);

	const struct format *fmt = wl_to_gl(wl_fmt);
	if (!fmt) {
		wlr_log(L_ERROR, "Unsupported pixel format");
		return false;
	}

	DEBUG_PUSH;

	// Make sure any pending drawing is finished before we try to read it
	glFinish();

	// Unfortunately GLES2 doesn't support GL_PACK_*, so we have to read
	// the lines out row by row

	unsigned char *p = data + dst_y * stride;
	for (size_t i = src_y; i < src_y + height; ++i) {
		glReadPixels(src_x, src_y + height - i - 1, width, 1, fmt->gl_fmt,
			fmt->gl_type, p + i * stride + dst_x * 4);
	}

	DEBUG_POP;
	return true;
}

void push_marker(const char *file, const char *func) {
	if (!glPushDebugGroupKHR) {
		return;
	}

	int len = snprintf(NULL, 0, "%s:%s", file, func) + 1;
	char str[len];
	snprintf(str, len, "%s:%s", file, func);
	glPushDebugGroupKHR(GL_DEBUG_SOURCE_APPLICATION_KHR, 1, -1, str);
}

void pop_marker(void) {
	if (glPopDebugGroupKHR) {
		glPopDebugGroupKHR();
	}
}

static log_importance_t gl_to_wlr(GLenum type) {
	switch (type) {
	case GL_DEBUG_TYPE_ERROR_KHR:               return L_ERROR;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_KHR: return L_DEBUG;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_KHR:  return L_ERROR;
	case GL_DEBUG_TYPE_PORTABILITY_KHR:         return L_DEBUG;
	case GL_DEBUG_TYPE_PERFORMANCE_KHR:         return L_DEBUG;
	case GL_DEBUG_TYPE_OTHER_KHR:               return L_INFO;
	case GL_DEBUG_TYPE_MARKER_KHR:              return L_DEBUG;
	case GL_DEBUG_TYPE_PUSH_GROUP_KHR:          return L_DEBUG;
	case GL_DEBUG_TYPE_POP_GROUP_KHR:           return L_DEBUG;
	default:                                    return L_INFO;
	}
}

static void gl_log(GLenum src, GLenum type, GLuint id, GLenum severity,
		GLsizei len, const GLchar *msg, const void *user) {
	_wlr_log(gl_to_wlr(type), "[GLES] %s", msg);
}

static GLuint compile_shader(GLuint type, const GLchar *src) {
	DEBUG_PUSH;
	GLuint shader = glCreateShader(type);

	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);

	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (success == GL_FALSE) {
		glDeleteShader(shader);
		shader = 0;
	}

	DEBUG_POP;
	return shader;
}

static GLuint link_program(const GLchar *vert_src, const GLchar *frag_src) {
	DEBUG_PUSH;
	GLuint prog = 0;

	GLuint vert = compile_shader(GL_VERTEX_SHADER, vert_src);
	if (!vert) {
		goto error;
	}

	GLuint frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
	if (!frag) {
		glDeleteShader(vert);
		goto error;
	}

	prog = glCreateProgram();
	glAttachShader(prog, vert);
	glAttachShader(prog, frag);
	glLinkProgram(prog);

	glDetachShader(prog, vert);
	glDetachShader(prog, frag);
	glDeleteShader(vert);
	glDeleteShader(frag);

	GLint success;
	glGetProgramiv(prog, GL_LINK_STATUS, &success);
	if (success == GL_FALSE) {
		glDeleteProgram(prog);
		prog = 0;
	}

error:
	DEBUG_POP;
	return prog;
}

extern const GLchar poly_vert_src[];
extern const GLchar poly_frag_src[];
extern const GLchar tex_vert_src[];
extern const GLchar tex_frag_src[];

struct wlr_render *wlr_render_create(struct wlr_backend *backend) {
	struct wlr_render *rend = calloc(1, sizeof(*rend));
	if (!rend) {
		return NULL;
	}

	rend->egl = wlr_backend_get_egl(backend);
	eglMakeCurrent(rend->egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		rend->egl->context);

	if (glDebugMessageCallbackKHR && glDebugMessageControlKHR) {
		glEnable(GL_DEBUG_OUTPUT_KHR);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
		glDebugMessageCallbackKHR(gl_log, NULL);

		// Silence unwanted message types
		glDebugMessageControlKHR(GL_DONT_CARE, GL_DEBUG_TYPE_POP_GROUP_KHR,
			GL_DONT_CARE, 0, NULL, GL_FALSE);
		glDebugMessageControlKHR(GL_DONT_CARE, GL_DEBUG_TYPE_PUSH_GROUP_KHR,
			GL_DONT_CARE, 0, NULL, GL_FALSE);
	}

	DEBUG_PUSH;

	rend->shaders.poly = link_program(poly_vert_src, poly_frag_src);
	if (!rend->shaders.poly) {
		goto error;
	}
	rend->shaders.tex = link_program(tex_vert_src, tex_frag_src);
	if (!rend->shaders.tex) {
		goto error;
	}

	DEBUG_POP;
	return rend;

error:
	glDeleteProgram(rend->shaders.poly);
	glDeleteProgram(rend->shaders.tex);

	DEBUG_POP;
	if (glDebugMessageCallbackKHR) {
		glDisable(GL_DEBUG_OUTPUT_KHR);
		glDebugMessageCallbackKHR(NULL, NULL);
	}

	free(rend);
	return NULL;
}

void wlr_render_destroy(struct wlr_render *rend) {
	free(rend);
}
