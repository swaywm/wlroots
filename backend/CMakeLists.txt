include_directories(
	${PROTOCOLS_INCLUDE_DIRS}
	${WAYLAND_INCLUDE_DIR}
)

add_library(wlr-backend
    wayland/backend.c
    wayland/registry.c
    wayland/wl_seat.c
    wayland/wl_output.c
)

target_link_libraries(wlr-backend
    wlr-common
    wlr-wayland
    ${WAYLAND_LIBRARIES}
)
