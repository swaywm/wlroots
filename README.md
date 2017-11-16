# wlroots [![](https://api.travis-ci.org/swaywm/wlroots.svg)](https://travis-ci.org/swaywm/wlroots)

Pluggable, composable modules for building a
[Wayland](http://wayland.freedesktop.org/) compositor.

This is a WIP: [status](https://github.com/swaywm/wlroots/issues/9)

## Contributing

See [CONTRIBUTING.md](https://github.com/swaywm/wlroots/blob/master/CONTRIBUTING.md)

## Building

Install dependencies:

* wayland
* wayland-protocols
* EGL
* GLESv2
* DRM
* GBM
* libinput
* udev
* pixman
* systemd (optional, for logind support)
* elogind (optional, for logind support on systems without systemd)
* libcap (optional, for capability support)
* asciidoc (optional, for man pages)

Run these commands:

    meson build
    ninja -C build

(On FreeBSD, you need to pass an extra flag to prevent a linking error: `meson build -D b_lundef=false`)

## Running the Reference Compositor

wlroots comes with a reference compositor called rootston that demonstrates the
features of the library.

After building, run rootston from a terminal or VT with:

    ./build/rootston/rootston

Now you can run windows in the compositor from the command line or by
configuring bindings in your
[`rootston.ini`](https://github.com/swaywm/wlroots/blob/master/rootston/rootston.ini.example)
file. 
