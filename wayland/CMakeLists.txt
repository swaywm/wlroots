include_directories(
	${PROTOCOLS_INCLUDE_DIRS}
	${WAYLAND_INCLUDE_DIR}
)

add_library(wlr-wayland
    types/wlr_wl_seat.c
    types/wlr_wl_output.c
)

target_link_libraries(wlr-wayland
    wlr-common
    ${WAYLAND_LIBRARIES}
)
