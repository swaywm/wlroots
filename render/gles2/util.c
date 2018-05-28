#include <GLES2/gl2.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include "render/gles2.h"

const char *gles2_strerror(GLenum err) {
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

bool _gles2_flush_errors(const char *file, int line) {
	GLenum err;
	bool failure = false;
	while ((err = glGetError()) != GL_NO_ERROR) {
		failure = true;
		if (err == GL_OUT_OF_MEMORY) {
			// The OpenGL context is now undefined
			_wlr_log(WLR_ERROR, "[%s:%d] Fatal GL error: out of memory", file, line);
			exit(1);
		} else {
			_wlr_log(WLR_ERROR, "[%s:%d] GL error %d %s", file, line, err, gles2_strerror(err));
		}
	}
	return failure;
}
