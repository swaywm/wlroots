xwayland_libs = []
xwayland_required = [
	'xcb',
	'xcb-composite',
	'xcb-render',
	'xcb-xfixes',
]
xwayland_optional = {
	'xcb-errors': 'Required for printing X11 errors.',
	'xcb-icccm': 'Required for extended X11 window manager hints.',
}

msg = []
if get_option('xwayland').enabled()
	msg += 'Install "@0@" or pass "-Dxwayland=disabled".'
endif
if not get_option('xwayland').disabled()
	msg += 'Required for Xwayland support.'
endif

foreach lib : xwayland_required
	dep = dependency(lib,
		required: get_option('xwayland'),
		not_found_message: '\n'.join(msg).format(lib),
	)
	if not dep.found()
		subdir_done()
	endif

	xwayland_libs += dep
endforeach

foreach lib, desc : xwayland_optional
	msg = []
	if get_option(lib).enabled()
		msg += 'Install "@0@" or pass "-D@0@=disabled".'
	endif
	if not get_option(lib).disabled()
		msg += desc
	endif

	dep = dependency(lib,
		required: get_option(lib),
		not_found_message: '\n'.join(msg).format(lib),
	)
	if dep.found()
		xwayland_libs += dep
		conf_data.set10('WLR_HAS_' + lib.underscorify().to_upper(), true)
	endif
endforeach

wlr_files += files(
	'selection/dnd.c',
	'selection/incoming.c',
	'selection/outgoing.c',
	'selection/selection.c',
	'sockets.c',
	'xwayland.c',
	'xwm.c',
)
wlr_deps += xwayland_libs
conf_data.set10('WLR_HAS_XWAYLAND', true)
