# wlroots

Pluggable, composable, unopinionated modules for building a
[Wayland](http://wayland.freedesktop.org/) compositor; or about 40,000 lines of
code you were going to write anyway.

- wlroots provides backends that abstract the underlying display and input
	hardware, including KMS/DRM, libinput, Wayland, X11, and headless backends,
	plus any custom backends you choose to write, which can all be created or
	destroyed at runtime and used in concert with each other.
- wlroots provides unopinionated, mostly standalone implementations of many
	Wayland interfaces, both from wayland.xml and various protocol extensions.
	We also promote the standardization of portable extensions across
	many compositors.
- wlroots provides several powerful, standalone, and optional tools that
	implement components common to many compositors, such as the arrangement of
	outputs in physical space.
- wlroots provides an Xwayland abstraction that allows you to have excellent
	Xwayland support without worrying about writing your own X11 window manager
	on top of writing your compositor.
- wlroots provides a renderer abstraction that simple compositors can use to
	avoid writing GL code directly, but which steps out of the way when your
	needs demand custom rendering code.

wlroots implements a huge variety of Wayland compositor features and implements
them *right*, so you can focus on the features that make your compositor
unique. By using wlroots, you get high performance, excellent hardware
compatibility, broad support for many wayland interfaces, and comfortable
development tools - or any subset of these features you like, because all of
them work independently of one another and freely compose with anything you want
to implement yourself.

Check out our [wiki](https://github.com/swaywm/wlroots/wiki/Getting-started) to
get started with wlroots.

wlroots is developed under the direction of the
[sway](https://github.com/swaywm/sway) project. A variety of wrapper libraries
[are available](https://github.com/swaywm) for using it with your favorite
programming language.

## Building

Install dependencies:

* meson
* wayland
* wayland-protocols
* EGL
* GLESv2
* libdrm
* GBM
* libinput
* xkbcommon
* udev
* pixman
* systemd (optional, for logind support)
* elogind (optional, for logind support on systems without systemd)
* libcap (optional, for capability support)

If you choose to enable X11 support:

* xcb
* xcb-composite
* xcb-xfixes
* xcb-xinput
* xcb-image
* xcb-render
* x11-xcb
* xcb-errors (optional, for improved error reporting)
* x11-icccm (optional, for improved Xwayland introspection)
* xcb-xkb (optional, for improved keyboard handling on the X11 backend)

Run these commands:

    meson build
    ninja -C build

Install like so:

	sudo ninja -C build install

## Running the test compositor

wlroots comes with a test compositor called rootston, which demonstrates the
features of the library and is used as a testbed for the development of the
library. It may also be useful as a reference for understanding how to use
various wlroots features, but it's not considered a production-quality codebase
and is not designed for daily use.

If you followed the build instructions above the rootston executable can be
found at `./build/rootston/rootston`. To use it, refer to the example config at
[./rootston/rootston.ini.example](https://github.com/swaywm/wlroots/blob/master/rootston/rootston.ini.example)
and place a config file of your own at `rootston.ini` in the working directory
(or in an arbitrary location via `rootston -C`). Other options are available,
refer to `rootston -h`.

## Contributing

See [CONTRIBUTING.md](https://github.com/swaywm/wlroots/blob/master/CONTRIBUTING.md).
