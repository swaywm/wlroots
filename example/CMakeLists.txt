include_directories(
	${DRM_INCLUDE_DIRS}
)

add_executable(example
    main.c
)

target_link_libraries(example
    wlr-backend
    wlr-session
)

add_executable(rotation
    rotation.c
)

target_link_libraries(rotation
    wlr-backend
    wlr-session
)
