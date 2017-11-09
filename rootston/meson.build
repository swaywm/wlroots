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
	'xcursor.c',
	'xdg_shell_v6.c',
	'wl_shell.c',
]
if get_option('enable_xwayland')
	sources += ['xwayland.c']
endif
executable(
	'rootston', sources, dependencies: [wlroots, wlr_protos]
)
