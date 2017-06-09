include_directories(
	${PROTOCOLS_INCLUDE_DIRS}
	${WAYLAND_INCLUDE_DIR}
	${DRM_INCLUDE_DIRS}
    ${LIBINPUT_INCLUDE_DIRS}
)

add_library(wlr-backend
    #wayland/backend.c
    #wayland/registry.c
    #wayland/wl_seat.c
    #wayland/wl_output.c

    drm/backend.c
    drm/drm.c

    libinput/backend.c

    backend.c
    egl.c
    udev.c
)

target_link_libraries(wlr-backend
    wlr-common
    wlr-types
    ${WAYLAND_LIBRARIES}
    ${DRM_LIBRARIES}
    ${GBM_LIBRARIES}
    ${GLESv2_LIBRARIES}
    ${EGL_LIBRARIES}
    ${SYSTEMD_LIBRARIES}
    ${UDEV_LIBRARIES}
    ${LIBINPUT_LIBRARIES}
    ${GBM_LIBRARIES}
)
