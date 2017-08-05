# wlroots [![](https://api.travis-ci.org/SirCmpwn/wlroots.svg)](https://travis-ci.org/SirCmpwn/wlroots)

Pluggable, composable modules for building a Wayland compositor.

WIP - [Status](https://github.com/SirCmpwn/wlroots/issues/9)

## Building

Install dependencies:

* cmake
* wayland
* wayland-protocols
* EGL
* GLESv2
* DRM
* GBM
* libinput
* udev
* systemd (optional, for logind support)
* libcap (optional, for capability support)
* asciidoc (optional, for man pages)

Run these commands:

    meson build
    ninja -C build
