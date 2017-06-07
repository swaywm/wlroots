include_directories(
	${DRM_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(simple
    simple.c
)

target_link_libraries(simple
    wlr-backend
    wlr-session
)

add_executable(rotation
    rotation.c
    cat.c
)

target_link_libraries(rotation
    wlr-backend
    wlr-session
)
