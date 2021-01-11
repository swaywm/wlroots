libinput = dependency('libinput', version: '>=1.14.0', required: get_option('libinput-backend'))
if not libinput.found()
	subdir_done()
endif

conf_data.set10('WLR_HAS_LIBINPUT_BACKEND', true)

wlr_deps += libinput

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
