add_library(wlr-common STATIC
	list.c
    log.c
)

target_link_libraries(wlr-common m)
