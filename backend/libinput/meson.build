libinput = dependency('libinput', version: '>=1.14.0',
                      required: meson.current_source_dir() in backends)

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

features += {'libinput-backend': true}
wlr_deps += libinput
