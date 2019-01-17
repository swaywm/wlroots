wlroots reads these environment variables

wlroots specific
----------------
* *WLR_DRM_DEVICES*: specifies the DRM devices (as a colon separated list)
  instead of auto probing them. The first existing device in this list is
  considered the primary DRM device.
* *WLR_DRM_NO_ATOMIC*: set to 1 to use legacy DRM interface instead of atomic
  mode setting
* *WLR_DRM_NO_ATOMIC_GAMMA*: set to 1 to use legacy DRM interface for gamma
  control instead of the atomic interface
* *WLR_LIBINPUT_NO_DEVICES*: set to 1 to not fail without any input devices
* *WLR_BACKENDS*: comma-separated list of backends to use (available backends:
  wayland, x11, headless, noop)
* *WLR_WL_OUTPUTS*: when using the wayland backend specifies the number of outputs
* *WLR_X11_OUTPUTS*: when using the X11 backend specifies the number of outputs
* *WLR_HEADLESS_OUTPUTS*: when using the headless backend specifies the number
  of outputs
* *WLR_NO_HARDWARE_CURSORS*: set to 1 to use software cursors instead of
  hardware cursors
* *WLR_SESSION*: specifies the wlr\_session to be used (available sessions:
  logind/systemd, direct)

rootston specific
------------------
* *XKB_DEFAULT_RULES*, *XKB_DEFAULT_MODEL*, *XKB_DEFAULT_LAYOUT*,
  *XKB_DEFAULT_VARIANT*, *XKB_DEFAULT_OPTIONS*: xkb setup

generic
-------
* *DISPLAY*: if set probe X11 backend in *wlr_backend_autocreate*
* *WAYLAND_DISPLAY*, *_WAYLAND_DISPLAY*, *WAYLAND_SOCKET*: if set probe Wayland
  backend in *wlr_backend_autocreate*
* *XCURSOR_PATH*: directory where xcursors are located
