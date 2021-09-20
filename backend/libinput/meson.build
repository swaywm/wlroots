msg = ['Required for libinput backend support.']
if 'libinput' in backends
	msg += 'Install "libinput" or disable the libinput backend.'
endif

libinput = dependency(
	'libinput',
	version: '>=1.14.0',
	required: 'libinput' in backends,
	not_found_message: '\n'.join(msg),
)

if not libinput.found()
	subdir_done()
endif

wlr_files += files(
	'backend.c',
	'events.c',
	'keyboard.c',
	'pointer.c',
	'switch.c',
	'tablet_pad.c',
	'tablet_tool.c',
	'touch.c',
)

features += { 'libinput-backend': true }
wlr_deps += libinput

# Check whether libinput supports high-resolution scroll or not
libinput_supports_scroll_value120 = cc.has_function(
	'libinput_event_pointer_get_scroll_value_v120',
	dependencies: libinput,
)

add_project_arguments(
	'-DLIBINPUT_HAS_SCROLL_VALUE120=@0@'.format(libinput_supports_scroll_value120.to_int()),
	language: 'c',
)
