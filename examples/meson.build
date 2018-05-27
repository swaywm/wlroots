threads = dependency('threads')
wayland_cursor = dependency('wayland-cursor')

libavutil = dependency('libavutil')
libavcodec = dependency('libavcodec')
libavformat = dependency('libavformat')

executable('simple', 'simple.c', dependencies: wlroots)
executable('pointer', 'pointer.c', dependencies: wlroots)
executable('touch', 'touch.c', 'cat.c', dependencies: wlroots)
executable('tablet', 'tablet.c', dependencies: wlroots)
executable('rotation', 'rotation.c', 'cat.c', dependencies: wlroots)
executable('multi-pointer', 'multi-pointer.c', dependencies: wlroots)
executable('output-layout', 'output-layout.c', 'cat.c', dependencies: wlroots)

executable(
	'screenshot',
	'screenshot.c',
	dependencies: [wayland_client, wlr_protos, wlroots]
)

executable(
	'idle',
	'idle.c',
	dependencies: [wayland_client, wlr_protos, wlroots, threads]
)

executable(
	'idle-inhibit',
	'idle-inhibit.c',
	dependencies: [wayland_client, wlr_protos, wlroots, threads]
)

executable(
	'layer-shell',
	'layer-shell.c',
	dependencies: [wayland_cursor, wayland_client, wlr_protos, wlroots]
)

executable(
	'input-inhibitor',
	'input-inhibitor.c',
	dependencies: [wayland_cursor, wayland_client, wlr_protos, wlroots]
)

executable(
	'dmabuf-capture',
	'dmabuf-capture.c',
	dependencies: [wayland_client, wlr_protos, libavutil, libavcodec, libavformat]
)
