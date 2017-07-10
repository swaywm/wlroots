# wlroots

Pluggable, composable modules for building a Wayland compositor.

WIP - [Status](https://github.com/SirCmpwn/wlroots/issues/9)

## Building

Install dependencies:

* meson
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

    meson --buildtype=release build
    cd build
    ninja
