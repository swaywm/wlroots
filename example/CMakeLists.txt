add_executable(example
    main.c
)

target_link_libraries(example
    wlr-backend
)

add_executable(example-drm
    example-drm.c
)

target_link_libraries(example-drm
    wlr-backend
    wlr-session
)
