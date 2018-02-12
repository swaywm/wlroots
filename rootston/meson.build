sources = [
	'config.c',
	'cursor.c',
	'desktop.c',
	'ini.c',
	'input.c',
	'keyboard.c',
	'main.c',
	'output.c',
	'seat.c',
	'wl_shell.c',
	'xdg_shell_v6.c',
]
if get_option('enable_xwayland')
	sources += ['xwayland.c']
endif
executable(
	'rootston', sources, dependencies: [wlroots, wlr_protos, pixman]
)
