include_directories(
    ${WAYLAND_INCLUDE_DIR}
    ${DRM_INCLUDE_DIRS}
    ${Libcap_INCLUDE_DIRS}
)

set(sources
    session.c
    direct.c
)

set(libs
    wlr-util
    ${WAYLAND_LIBRARIES}
    ${Libcap_LIBRARIES}
)

if (SYSTEMD_FOUND)
    add_definitions(${SYSTEMD_DEFINITIONS})
    include_directories(${SYSTEMD_INCLUDE_DIRS})

    add_definitions(-DHAS_SYSTEMD)
    list(APPEND sources logind.c)
    list(APPEND libs ${SYSTEMD_LIBRARIES})
endif ()

add_library(wlr-session ${sources})
target_link_libraries(wlr-session ${libs})
