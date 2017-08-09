lib_shared = static_library('shared',
    ['shared.c', 'cat.c'],
    dependencies: dep_wlr)

executable('simple', 'simple.c', dependencies: dep_wlr, link_with: lib_shared)
executable('rotation', 'rotation.c', dependencies: dep_wlr, link_with: lib_shared)
executable('pointer', 'pointer.c', dependencies: dep_wlr, link_with: lib_shared)
executable('touch', 'touch.c', dependencies: dep_wlr, link_with: lib_shared)
executable('tablet', 'tablet.c', dependencies: dep_wlr, link_with: lib_shared)

wayland_scanner = find_program('wayland-scanner')
wl_protocol_dir = dep_wayland_proto.get_pkgconfig_variable('pkgdatadir')

wayland_scanner_server = generator(wayland_scanner,
		output: '@BASENAME@-protocol.h',
		arguments: ['server-header', '@INPUT@', '@OUTPUT@'])

wayland_scanner_code = generator(wayland_scanner,
		output: '@BASENAME@-protocol.c',
		arguments: ['code', '@INPUT@', '@OUTPUT@'])

compositor_src = [
    'compositor/main.c',
    'compositor/wl_compositor.c',
    'compositor/wl_shell.c',
    'compositor/xdg_shell.c',
]

protocols = [
	[ 'unstable', 'xdg-shell', 'xdg-shell-unstable-v6.xml' ]
]

foreach p : protocols
	xml = join_paths([wl_protocol_dir] + p)
	compositor_src += wayland_scanner_code.process(xml)
	compositor_src += wayland_scanner_server.process(xml)
endforeach

executable('compositor', compositor_src, dependencies: dep_wlr, link_with: lib_shared)
