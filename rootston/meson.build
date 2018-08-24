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
	'virtual_keyboard.c',
	'wl_shell.c',
	'xdg_shell_v6.c',
	'xdg_shell.c',
]
if get_option('enable-xwayland')
	sources += ['xwayland.c']
endif
executable(
	'rootston', sources, dependencies: [wlroots, wlr_protos, pixman]
)
