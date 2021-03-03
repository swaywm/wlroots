have_listenfd = false
if xwayland.found()
	xwayland_path = xwayland.get_pkgconfig_variable('xwayland')
	have_listenfd = xwayland.get_pkgconfig_variable('have_listenfd') == 'true'
else
	xwayland_path = xwayland_prog.full_path()
endif

xwayland_config_data = configuration_data()
xwayland_config_data.set_quoted('XWAYLAND_PATH', xwayland_path)
xwayland_config_data.set10('HAVE_XWAYLAND_LISTENFD', have_listenfd)
configure_file(
	output: 'config.h',
	configuration: xwayland_config_data,
)
