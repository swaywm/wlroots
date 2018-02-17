lib_shared = static_library(
	'shared',
	['support/shared.c', 'support/cat.c', 'support/ini.c', 'support/config.c'],
	dependencies: wlroots,
	include_directories: include_directories('support')
)

threads = dependency('threads')

executable('simple', 'simple.c', dependencies: wlroots, link_with: lib_shared)
executable('pointer', 'pointer.c', dependencies: wlroots, link_with: lib_shared)
executable('touch', 'touch.c', dependencies: wlroots, link_with: lib_shared)
executable('tablet', 'tablet.c', dependencies: wlroots, link_with: lib_shared)
executable('multi-pointer', 'multi-pointer.c', dependencies: wlroots,
	link_with: lib_shared)

executable(
	'rotation',
	'rotation.c',
	dependencies: wlroots,
	link_with: lib_shared,
)

executable(
	'output-layout',
	'output-layout.c',
	dependencies: wlroots,
	link_with: lib_shared,
)

executable(
	'screenshot',
	'screenshot.c',
	dependencies: [wayland_client, wlr_protos, wlroots],
	link_with: lib_shared,
)

executable(
	'idle',
	'idle.c',
	dependencies: [wayland_client, wlr_protos, wlroots, threads],
	link_with: lib_shared,
)

executable(
	'idle-inhibit',
	'idle-inhibit.c',
	dependencies: [wayland_client, wlr_protos, wlroots, threads],
	link_with: lib_shared,
)
