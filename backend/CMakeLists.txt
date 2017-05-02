include_directories(
	${PROTOCOLS_INCLUDE_DIRS}
	${WAYLAND_INCLUDE_DIR}
	${DRM_INCLUDE_DIRS}
)

add_library(wlr-backend
    wayland/backend.c
    wayland/registry.c
    wayland/wl_seat.c
    wayland/wl_output.c
    drm/backend.c
    drm/drm.c
    drm/session.c
    drm/udev.c
)

target_link_libraries(wlr-backend
    wlr-common
    wlr-wayland
    ${WAYLAND_LIBRARIES}
)
