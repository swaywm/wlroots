sources = [
	'bindings.c',
	'config.c',
	'cursor.c',
	'desktop.c',
	'ini.c',
	'input.c',
	'keyboard.c',
	'layer_shell.c',
	'main.c',
	'output.c',
	'render.c',
	'seat.c',
	'switch.c',
	'text_input.c',
	'view.c',
	'virtual_keyboard.c',
	'wl_shell.c',
	'xdg_shell_v6.c',
	'xdg_shell.c',
]

if conf_data.get('WLR_HAS_XWAYLAND', 0) == 1
	sources += 'xwayland.c'
endif

executable(
	'rootston',
	sources,
	dependencies: [wlroots, wlr_protos, pixman],
	build_by_default: get_option('rootston'),
)
