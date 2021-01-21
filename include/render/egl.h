#ifndef RENDER_EGL_H
#define RENDER_EGL_H

#include <wlr/render/egl.h>

/**
 * Creates and EGL context from a given drm fd. This function uses the
 * following extensions:
 * - EXT_device_enumeration to get the list of available EGLDeviceEXT
 * - EXT_device_drm to get the name of the EGLDeviceEXT
 * - EXT_platform_device to create an EGLDisplay from an EGLDeviceEXT
 */
struct wlr_egl *wlr_egl_create_from_drm_fd(int drm_fd);

#endif
