executable(
	'tinywl',
	['tinywl.c', protocols_server_header['xdg-shell'], protocols_code['xdg-shell']],
	dependencies: wlroots,
	include_directories: [wlr_inc, proto_inc],
)
