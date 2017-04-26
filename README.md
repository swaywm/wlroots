# wlroots

A set of utility libraries for Wayland compositors.

## Building wlroots.

In a directory other than the source directory run

`cmake /path/to/source`

Then run `make`.

## Running wlroots.

Build wlroots. Make sure a wayland compositor is running. Navigate to the `bin` directory in your build folder (from above) and execute `example`. Ideal ouput should look something like this:

```
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: wl_compositor v4
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: wl_subcompositor v1
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: wp_viewporter v1
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: wp_presentation v1
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: zwp_relative_pointer_manager_v1 v1
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: zwp_pointer_constraints_v1 v1
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: wl_data_device_manager v3
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: wl_shm v1
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: wl_seat v5
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: wl_output v3
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: zwp_input_panel_v1 v1
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: zwp_input_method_v1 v1
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: zwp_text_input_manager_v1 v1
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: zxdg_shell_v6 v1
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: xdg_shell v1
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: wl_shell v1
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: weston_desktop_shell v1
04/26/17 17:13:11 - [registry.c:57] Remote wayland global: weston_screenshooter v1
04/26/17 17:13:11 - [wl_seat.c:18] seat 0x1acd7f0 offered pointer
04/26/17 17:13:11 - [wl_seat.c:31] seat 0x1acd7f0 offered keyboard
04/26/17 17:13:11 - [wl_output.c:45] Got info for output 0x1acc510 0x0 (270mm x 158mm) weston-X11 none
04/26/17 17:13:11 - [wl_output.c:52] Got scale factor for output 0x1acc510: 1
04/26/17 17:13:11 - [wl_output.c:27] Got mode for output 0x1acc510: 1024x600 @ 60.00Hz (preferred) (current)

```

## Current TODO

* [ ] Implement wl_pointer.c
* [ ] Implement wl_keyboard.c
* [ ] Implement client shm buffer management
* [ ] Implement a surface (requires xdg_shell)

After that:

- compositing code
	- EGL/gles2 context rendered to backend surface
- backend needs wl_outputs (for internal wayland server to use)
- other nebulous things 

