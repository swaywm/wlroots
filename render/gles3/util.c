#include <stdlib.h>
#include <stdbool.h>
#include <GLES3/gl3.h>
#include <wlr/util/log.h>
#include "render/gles3.h"

const char *gles3_strerror(GLenum err) {
	switch (err) {
	case GL_INVALID_ENUM:
		return "Invalid enum";
	case GL_INVALID_VALUE:
		return "Invalid value";
	case GL_INVALID_OPERATION:
		return "Invalid operation";
	case GL_OUT_OF_MEMORY:
		return "Out of memory";
	case GL_INVALID_FRAMEBUFFER_OPERATION:
		return "Invalid framebuffer operation";
	default:
		return "Unknown error";
	}
}

bool _gles3_flush_errors(const char *file, int line) {
	GLenum err;
	bool failure = false;
	while ((err = glGetError()) != GL_NO_ERROR) {
		failure = true;
		if (err == GL_OUT_OF_MEMORY) {
			// The OpenGL context is now undefined
			_wlr_log(L_ERROR, "[%s:%d] Fatal GL error: out of memory", file, line);
			exit(1);
		} else {
			_wlr_log(L_ERROR, "[%s:%d] GL error %d %s", file, line, err, gles3_strerror(err));
		}
	}
	return failure;
}
