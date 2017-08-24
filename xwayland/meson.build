lib_wlr_xwayland = static_library('wlr_xwayland', files(
    'sockets.c',
    'xwayland.c',
    'xwm.c',
  ),
  include_directories: wlr_inc,
  dependencies: [wayland_server, xcb, xcb_composite, pixman])
