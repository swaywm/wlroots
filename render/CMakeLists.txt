add_library(wlr-render STATIC
	matrix.c
    wlr_renderer.c
    wlr_surface.c
    gles2/shaders.c
    gles2/renderer.c
    gles2/surface.c
    gles2/pixel_format.c
    gles2/util.c
)
