wlr_files += files(
    'backend.c',
    'egl.c',
    'udev.c',
    'session/direct-ipc.c',
    'session/direct.c',
    'session/session.c',
    'drm/backend.c',
    'drm/drm.c',
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

if dep_systemd.found()
    wlr_files += files('session/logind.c')
endif
