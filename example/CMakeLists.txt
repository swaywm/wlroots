include_directories(
	${DRM_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${XKBCOMMON_INCLUDE_DIRS}
)

add_executable(simple
    simple.c
    shared.c
)

target_link_libraries(simple
    wlr-backend
    wlr-session
    ${XKBCOMMON_LIBRARIES}
)

add_executable(rotation
    rotation.c
    shared.c
    cat.c
)

target_link_libraries(rotation
    wlr-backend
    wlr-session
    wlr-render
    ${XKBCOMMON_LIBRARIES}
)

add_executable(pointer
    pointer.c
    shared.c
    cat.c
)

target_link_libraries(pointer
    wlr-backend
    wlr-session
    wlr-render
    ${XKBCOMMON_LIBRARIES}
)

add_executable(touch
    touch.c
    shared.c
    cat.c
)

target_link_libraries(touch
    wlr-backend
    wlr-session
    wlr-render
    ${XKBCOMMON_LIBRARIES}
)

add_executable(tablet
    tablet.c
    shared.c
)

target_link_libraries(tablet
    wlr-backend
    wlr-session
    wlr-render
    ${XKBCOMMON_LIBRARIES}
)

add_executable(compositor
    compositor
    shared.c
)

target_link_libraries(compositor
    wlr-backend
    wlr-session
    wlr-render
    wlr-wayland
    ${XKBCOMMON_LIBRARIES}
)
