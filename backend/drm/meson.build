if get_option('drm-backend').disabled()
	subdir_done()
endif

conf_data.set10('WLR_HAS_DRM_BACKEND', true)

wlr_files += files(
	'atomic.c',
	'backend.c',
	'cvt.c',
	'drm.c',
	'legacy.c',
	'properties.c',
	'renderer.c',
	'util.c',
)
