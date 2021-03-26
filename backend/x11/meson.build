x11_libs = []
x11_required = [
	'xcb',
	'xcb-dri3',
	'xcb-present',
	'xcb-render',
	'xcb-renderutil',
	'xcb-shm',
	'xcb-xfixes',
	'xcb-xinput',
]

msg = []

if 'x11' in backends
	msg += 'Install "@0@" or don\'t enable X11 backend.'
else
	msg += 'Due to lack of "@0@", X11 backend support is disabled.'
endif

foreach lib : x11_required
	dep = dependency(lib,
                required: meson.current_source_dir() in backends,
		not_found_message: '\n'.join(msg).format(lib),
	)
	if not dep.found()
		subdir_done()
	endif

	x11_libs += dep
endforeach

wlr_files += files(
	'backend.c',
	'input_device.c',
	'output.c',
)
wlr_deps += x11_libs
features += { 'x11-backend': true }
