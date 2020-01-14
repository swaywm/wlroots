wlroots reads these environment variables

# wlroots specific

* *WLR_BACKENDS*: comma-separated list of backends to use (available backends:
  libinput, drm, wayland, x11, headless, noop)
* *WLR_NO_HARDWARE_CURSORS*: set to 1 to use software cursors instead of
  hardware cursors
* *WLR_SESSION*: specifies the wlr\_session to be used (available sessions:
  logind/systemd, direct)
* *WLR_DIRECT_TTY*: specifies the tty to be used (instead of using /dev/tty)

## DRM backend

* *WLR_DRM_DEVICES*: specifies the DRM devices (as a colon separated list)
  instead of auto probing them. The first existing device in this list is
  considered the primary DRM device.
* *WLR_DRM_NO_ATOMIC*: set to 1 to use legacy DRM interface instead of atomic
  mode setting

## Headless backend

* *WLR_HEADLESS_OUTPUTS*: when using the headless backend specifies the number
  of outputs

## libinput backend

* *WLR_LIBINPUT_NO_DEVICES*: set to 1 to not fail without any input devices

## Wayland backend

* *WLR_WL_OUTPUTS*: when using the wayland backend specifies the number of outputs

## X11 backend

* *WLR_X11_OUTPUTS*: when using the X11 backend specifies the number of outputs

# Generic

* *DISPLAY*: if set probe X11 backend in *wlr_backend_autocreate*
* *WAYLAND_DISPLAY*, *_WAYLAND_DISPLAY*, *WAYLAND_SOCKET*: if set probe Wayland
  backend in *wlr_backend_autocreate*
* *XCURSOR_PATH*: directory where xcursors are located
