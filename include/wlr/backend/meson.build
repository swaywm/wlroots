install_headers(
	'drm.h',
	'headless.h',
	'interface.h',
	'libinput.h',
	'multi.h',
	'noop.h',
	'session.h',
	'wayland.h',
	subdir: 'wlr/backend',
)

if conf_data.get('WLR_HAS_X11_BACKEND', 0) == 1
	install_headers('x11.h', subdir: 'wlr/backend')
endif

subdir('session')
