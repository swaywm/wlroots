wlroots reads these environment variables

# wlroots specific

* *WLR_DRM_DEVICES*: specifies the DRM devices (as a colon separated list)
  instead of auto probing them. The first existing device in this list is
  considered the primary DRM device.
* *WLR_DRM_NO_ATOMIC*: set to 1 to use legacy DRM interface instead of atomic
  mode setting
* *WLR_LIBINPUT_NO_DEVICES*: set to 1 to not fail without any input devices
* *WLR_BACKENDS*: comma-separated list of backends to use (available backends:
  drm, wayland, x11, rdp, headless, noop)
* *WLR_NO_HARDWARE_CURSORS*: set to 1 to use software cursors instead of
  hardware cursors
* *WLR_SESSION*: specifies the wlr\_session to be used (available sessions:
  logind/systemd, direct)
* *WLR_DIRECT_TTY*: specifies the tty to be used (instead of using /dev/tty)

# Headless backend

* *WLR_HEADLESS_OUTPUTS*: when using the headless backend specifies the number
  of outputs

# RDP backend

* *WLR_RDP_TLS_CERT_PATH*: required when using `wlr_backend_autocreate`,
  specifies the path to the TLS certificate to use for encrypting connections
* *WLR_RDP_TLS_KEY_PATH*: required when using `wlr_backend_autocreate`,
  specifies the path to the TLS private key to use for encrypting connections
* *WLR_RDP_ADDRESS*: the IP address to bind to, defaults to `127.0.0.1`
* *WLR_RDP_PORT*: the port to bind to, defaults to 3389.

Note: a TLS certificate and key can be generated like so:

```
$ openssl genrsa -out tls.key 2048
$ openssl req -new -key tls.key -out tls.csr
$ openssl x509 -req -days 365 -signkey tls.key -in tls.csr -out tls.crt
```

`tls.csr` can be discarded. Connecting to the RDP backend with xfreedrp can be
done like so:

	xfreerdp -v localhost --bpp 32 --size 1920x1080 --rfx

# Wayland backend

* *WLR_WL_OUTPUTS*: when using the wayland backend specifies the number of outputs

# X11 backend

* *WLR_X11_OUTPUTS*: when using the X11 backend specifies the number of outputs

# Rootston specific

* *XKB_DEFAULT_RULES*, *XKB_DEFAULT_MODEL*, *XKB_DEFAULT_LAYOUT*,
  *XKB_DEFAULT_VARIANT*, *XKB_DEFAULT_OPTIONS*: xkb setup

# Generic

* *DISPLAY*: if set probe X11 backend in *wlr_backend_autocreate*
* *WAYLAND_DISPLAY*, *_WAYLAND_DISPLAY*, *WAYLAND_SOCKET*: if set probe Wayland
  backend in *wlr_backend_autocreate*
* *XCURSOR_PATH*: directory where xcursors are located
