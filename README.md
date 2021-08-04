# wlroots

Pluggable, composable, unopinionated modules for building a [Wayland]
compositor; or about 50,000 lines of code you were going to write anyway.

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

Check out our [wiki] to get started with wlroots. Join our IRC channel:
[#sway-devel on Libera Chat].

wlroots is developed under the direction of the [sway] project. A variety of
[wrapper libraries] are available for using it with your favorite programming
language.

## Building

Install dependencies:

* [meson]
* [Wayland], for example:
  * Both [libwayland] and [wayland-protocols]
* [EGL] and [GLESv2], for example:
  * *libEGL* and *libGLESv2*, as provided by:
    * [mesa] alone
    * or [libglvnd] combined with a vendor driver (perhaps [mesa] again)
* [libdrm]
* *libgbm*, for example:
  * [mesa]
  * or [minigbm]
* [libinput]
* [libxkbcommon]
* *udev*, for example:
  * [systemd]
  * or [eudev]
* [pixman]
* [libseat]

If you choose to enable X11 support:

* [XWayland] (build-time only, optional at runtime)
* [libxcb]
* [libxcb-render-util]
* [libxcb-wm]
* [libxcb-errors] (optional, for improved error reporting)

Run these commands:

    meson build/
    ninja -C build/

Install like so:

    sudo ninja -C build/ install

## Contributing

See [CONTRIBUTING.md].

[Wayland]: https://wayland.freedesktop.org/
[wiki]: https://github.com/swaywm/wlroots/wiki/Getting-started
[#sway-devel on Libera Chat]: https://web.libera.chat/?channels=#sway-devel
[Sway]: https://github.com/swaywm/sway
[meson]: https://mesonbuild.com/
[libwayland]: https://gitlab.freedesktop.org/wayland/wayland
[wayland-protocols]: https://gitlab.freedesktop.org/wayland/wayland-protocols
[EGL]: https://www.khronos.org/egl
[GLESv2]: https://www.khronos.org/opengles/
[libglvnd]: https://gitlab.freedesktop.org/glvnd/libglvnd
[mesa]: https://gitlab.freedesktop.org/mesa/mesa
[minigbm]: https://github.com/intel/minigbm
[libdrm]: https://gitlab.freedesktop.org/mesa/drm
[libinput]: https://gitlab.freedesktop.org/libinput/libinput
[libxkbcommon]: https://github.com/xkbcommon/libxkbcommon
[systemd]: https://github.com/systemd/systemd
[eudev]: https://github.com/gentoo/eudev
[wrapper libraries]: https://github.com/search?q=topic%3Abindings+org%3Aswaywm&type=Repositories
[pixman]: http://www.pixman.org/
[libseat]: https://git.sr.ht/~kennylevinsen/seatd
[XWayland]: https://gitlab.freedesktop.org/xorg/xserver
[libxcb]: https://gitlab.freedesktop.org/xorg/lib/libxcb
[libxcb-wm]: https://gitlab.freedesktop.org/xorg/lib/libxcb-wm
[libxcb-render-util]: https://gitlab.freedesktop.org/xorg/lib/libxcb-render-util
[libxcb-errors]: https://gitlab.freedesktop.org/xorg/lib/libxcb-errors
[CONTRIBUTING.md]: https://github.com/swaywm/wlroots/blob/master/CONTRIBUTING.md
