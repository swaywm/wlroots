lib_shared = static_library('shared',
    ['shared.c', 'cat.c'],
    dependencies: dep_wlr)

executable('simple', 'simple.c', dependencies: dep_wlr, link_with: lib_shared)
executable('rotation', 'rotation.c', dependencies: dep_wlr, link_with: lib_shared)
executable('pointer', 'pointer.c', dependencies: dep_wlr, link_with: lib_shared)
executable('touch', 'touch.c', dependencies: dep_wlr, link_with: lib_shared)
executable('tablet', 'tablet.c', dependencies: dep_wlr, link_with: lib_shared)

compositor_src = [
    'compositor/main.c',
    'compositor/wl_compositor.c',
    'compositor/wl_shell.c',
    'compositor/xdg_shell.c',
	'compositor/protocols/xdg-shell.c',
]

wayland_scanner = find_program('wayland-scanner')
wayland_protocols_pkgdatadir = dep_wayland_proto.get_pkgconfig_variable('pkgdatadir')

protocols_src = meson.current_source_dir() + '/compositor/protocols'
run_command(['mkdir', '-p', protocols_src])

protocols = [
	['/unstable/xdg-shell/xdg-shell-unstable-v6.xml', 'xdg-shell']
]

foreach p : protocols
	xml = wayland_protocols_pkgdatadir + p[0]

	run_command([wayland_scanner, 'code', xml,
		protocols_src + '/' + p[1] + '.c'])

	run_command([wayland_scanner, 'server-header', xml,
		protocols_src + '/' + p[1] + '.h'])
endforeach

executable('compositor', compositor_src, dependencies: dep_wlr, link_with: lib_shared)
