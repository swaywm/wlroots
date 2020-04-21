wlr_files += files(
	'backend.c',
	'output.c',
	'seat.c',
	'tablet_v2.c',
)

client_protos = [
	'linux-dmabuf-unstable-v1',
	'pointer-gestures-unstable-v1',
	'presentation-time',
	'relative-pointer-unstable-v1',
	'tablet-unstable-v2',
	'xdg-decoration-unstable-v1',
	'xdg-shell',
]

foreach proto : client_protos
	wlr_files += protocols_client_header[proto]
endforeach
