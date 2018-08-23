sources = [
	'config.c',
	'cursor.c',
	'desktop.c',
	'ini.c',
	'input.c',
	'keyboard.c',
	'layer_shell.c',
	'main.c',
	'output.c',
	'seat.c',
	'text_input.c',
	'virtual_keyboard.c',
	'wl_shell.c',
	'xdg_shell.c',
	'xdg_shell_v6.c',
]

if conf_data.get('WLR_HAS_XWAYLAND', false)
	sources += 'xwayland.c'
endif

executable(
	'rootston',
	sources,
	dependencies: [wlroots, wlr_protos, pixman],
	build_by_default: get_option('rootston'),
)
