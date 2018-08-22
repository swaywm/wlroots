wl_protocol_dir = wayland_protos.get_pkgconfig_variable('pkgdatadir')

wayland_scanner_dep = dependency('wayland-scanner', required: false, native: true)
if wayland_scanner_dep.found()
	wayland_scanner = find_program(
		wayland_scanner_dep.get_pkgconfig_variable('wayland_scanner'),
		native: true,
	)
else
	wayland_scanner = find_program('wayland-scanner', native: true)
endif

wayland_scanner_server = generator(
	wayland_scanner,
	output: '@BASENAME@-protocol.h',
	arguments: ['server-header', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_code = generator(
	wayland_scanner,
	output: '@BASENAME@-protocol.c',
	arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_client = generator(
	wayland_scanner,
	output: '@BASENAME@-client-protocol.h',
	arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)

protocols = [
	[wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml'],
	[wl_protocol_dir, 'unstable/idle-inhibit/idle-inhibit-unstable-v1.xml'],
	[wl_protocol_dir, 'unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml'],
	[wl_protocol_dir, 'unstable/tablet/tablet-unstable-v2.xml'],
	[wl_protocol_dir, 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml'],
	[wl_protocol_dir, 'unstable/xdg-output/xdg-output-unstable-v1.xml'],
	[wl_protocol_dir, 'unstable/xdg-shell/xdg-shell-unstable-v6.xml'],
	'gamma-control.xml',
	'gtk-primary-selection.xml',
	'idle.xml',
	'screenshooter.xml',
	'server-decoration.xml',
	'virtual-keyboard-unstable-v1.xml',
	'wlr-export-dmabuf-unstable-v1.xml',
	'wlr-gamma-control-unstable-v1.xml',
	'wlr-input-inhibitor-unstable-v1.xml',
	'wlr-layer-shell-unstable-v1.xml',
	'wlr-screencopy-unstable-v1.xml',
]

client_protocols = [
	[wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml'],
	[wl_protocol_dir, 'unstable/idle-inhibit/idle-inhibit-unstable-v1.xml'],
	[wl_protocol_dir, 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml'],
	[wl_protocol_dir, 'unstable/xdg-shell/xdg-shell-unstable-v6.xml'],
	'idle.xml',
	'screenshooter.xml',
	'wlr-export-dmabuf-unstable-v1.xml',
	'wlr-gamma-control-unstable-v1.xml',
	'wlr-input-inhibitor-unstable-v1.xml',
	'wlr-layer-shell-unstable-v1.xml',
	'wlr-screencopy-unstable-v1.xml',
]

wl_protos_src = []
wl_protos_headers = []

foreach p : protocols
	xml = join_paths(p)
	wl_protos_src += wayland_scanner_code.process(xml)
	wl_protos_headers += wayland_scanner_server.process(xml)
endforeach

foreach p : client_protocols
	xml = join_paths(p)
	wl_protos_headers += wayland_scanner_client.process(xml)
endforeach

lib_wl_protos = static_library(
	'wl_protos',
	wl_protos_src + wl_protos_headers,
	dependencies: wayland_client.partial_dependency(includes: true),
)

wlr_protos = declare_dependency(
	link_with: lib_wl_protos,
	sources: wl_protos_headers,
)
