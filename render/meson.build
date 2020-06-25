wlr_files += files(
	'dmabuf.c',
	'egl.c',
	'drm_format_set.c',
	'gles2/pixel_format.c',
	'gles2/renderer.c',
	'gles2/shaders.c',
	'gles2/texture.c',
	'sync_file.c',
	'wlr_renderer.c',
	'wlr_texture.c',
)

conf_data.set10('WLR_HAS_LINUX_SYNC_FILE', cc.has_header('linux/sync_file.h'))
