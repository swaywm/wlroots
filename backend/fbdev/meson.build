if not get_option('fbdev-backend').enabled()
	conf_data.set10('WLR_HAS_FBDEV_BACKEND', false)
	subdir_done()
endif

wlr_files += files(
	'backend.c',
	'output.c',
)
conf_data.set10('WLR_HAS_FBDEV_BACKEND', true)
