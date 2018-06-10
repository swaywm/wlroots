threads = dependency('threads')
wayland_cursor = dependency('wayland-cursor')

libpng = dependency('libpng', required: false)

# These versions correspond to ffmpeg 4.0
libavutil = dependency('libavutil', version: '>=56.14.100', required: false)
libavcodec = dependency('libavcodec', version: '>=58.18.100', required: false)
libavformat = dependency('libavformat', version: '>=58.12.100', required: false)

if not cc.has_header('libavutil/hwcontext_drm.h', dependencies: libavutil)
    libavutil = disabler()
endif

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

if libavutil.found() and libavcodec.found() and libavformat.found()
	executable(
		'dmabuf-capture',
		'dmabuf-capture.c',
		dependencies: [wayland_client, wlr_protos, libavutil, libavcodec,
				libavformat, wlroots, threads ]
	)
endif

if libpng.found()
	executable(
		'screencopy',
		'screencopy.c',
		dependencies: [wayland_client, wlr_protos, wlroots, libpng]
	)
endif

executable(
	'toplevel-decoration',
	'toplevel-decoration.c',
	dependencies: [wayland_client, wlr_protos, wlroots]
)
