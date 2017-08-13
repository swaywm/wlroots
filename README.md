# wlroots [![](https://api.travis-ci.org/SirCmpwn/wlroots.svg)](https://travis-ci.org/SirCmpwn/wlroots)

Pluggable, composable modules for building a
[Wayland](http://wayland.freedesktop.org/) compositor.

This is a WIP: [status](https://github.com/SirCmpwn/wlroots/issues/9)

## Contributing

Development is organized in our [IRC
channel](http://webchat.freenode.net/?channels=sway&uio=d4), #sway on
irc.freenode.net. Join us and ask how you can help!

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
* libcap (optional, for capability support)
* asciidoc (optional, for man pages)

Run these commands:

    meson build
    ninja -C build
