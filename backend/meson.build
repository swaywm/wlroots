backend_files = files(
  'backend.c',
  'udev.c',
  'session/direct-ipc.c',
  'session/direct.c',
  'session/session.c',
  #'drm/backend.c',
  #'drm/drm.c',
  #'drm/drm-atomic.c',
  #'drm/drm-legacy.c',
  #'drm/drm-properties.c',
  #'drm/drm-util.c',
  'libinput/backend.c',
  'libinput/events.c',
  'libinput/keyboard.c',
  'libinput/pointer.c',
  'libinput/tablet_pad.c',
  'libinput/tablet_tool.c',
  'libinput/touch.c',
  'multi/backend.c',
  'wayland/backend.c',
  'wayland/output.c',
  'wayland/registry.c',
  'wayland/wl_seat.c',
)

if systemd.found()
  wlr_files += files('session/logind.c')
endif

lib_wlr_backend = static_library('wlr_backend', backend_files,
  include_directories: wlr_inc,
  dependencies: [wayland_server, egl, gbm, libinput, systemd])
