add_library(wlr-render STATIC
	matrix.c
    wlr_renderer.c
    wlr_surface.c
    gles3/shaders.c
    gles3/renderer.c
    gles3/surface.c
    gles3/util.c
)
