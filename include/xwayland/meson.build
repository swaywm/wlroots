have_listenfd = false
have_terminate_delay = false
if xwayland.found()
	xwayland_path = xwayland.get_variable(pkgconfig: 'xwayland')
	have_listenfd = xwayland.get_variable(pkgconfig: 'have_listenfd') == 'true'
	have_terminate_delay = xwayland.get_variable(pkgconfig: 'have_terminate_delay') == 'true'
else
	xwayland_path = xwayland_prog.full_path()
endif

xwayland_config_data = configuration_data()
xwayland_config_data.set_quoted('XWAYLAND_PATH', xwayland_path)
xwayland_config_data.set10('HAVE_XWAYLAND_LISTENFD', have_listenfd)
xwayland_config_data.set10('HAVE_XWAYLAND_TERMINATE_DELAY', have_terminate_delay)
configure_file(
	output: 'config.h',
	configuration: xwayland_config_data,
)
