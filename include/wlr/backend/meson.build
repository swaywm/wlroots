install_headers(
	'drm.h',
	'headless.h',
	'interface.h',
	'libinput.h',
	'multi.h',
	'session.h',
	'wayland.h',
	subdir: 'wlr/backend',
)

if conf_data.get('WLR_HAS_X11_BACKEND', false)
	install_headers('x11.h', subdir: 'wlr/backend')
endif

subdir('session')
