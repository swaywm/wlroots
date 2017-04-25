include_directories(
	${PROTOCOLS_INCLUDE_DIRS}
	${WAYLAND_INCLUDE_DIR}
    .
)

add_library(wlr-backend-wayland
    backend.c
)

target_link_libraries(wlr-backend-wayland
    ${WAYLAND_LIBRARIES}
)
