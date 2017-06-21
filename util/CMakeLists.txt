add_library(wlr-util STATIC
	list.c
    log.c
)

target_link_libraries(wlr-util m)
