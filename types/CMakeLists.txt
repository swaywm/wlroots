include_directories(
	${PROTOCOLS_INCLUDE_DIRS}
	${WAYLAND_INCLUDE_DIR}
)

add_library(wlr-types
    wlr_output.c
    wlr_input_device.c
    wlr_keyboard.c
    wlr_pointer.c
    wlr_touch.c
    wlr_tablet_tool.c
    wlr_tablet_pad.c
)

target_link_libraries(wlr-types
    wlr-util
    ${WAYLAND_LIBRARIES}
)
