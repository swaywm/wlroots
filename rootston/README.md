# rootston

Rootston is the "big" wlroots test compositor. It implements basically every
feature of wlroots and may be useful as a reference for new compositors.
However, it's mostly used as a testbed for wlroots development and does not have
particularly clean code and is not particularly well designed: proceed with a
grain of salt. It is not designed for end-users.

## Running rootston

If you followed the build instructions in `../README.md`, the rootston
executable can be found at `build/rootston/rootston`. To use it, refer to the
example config at [rootston/rootston.ini.example][rootston.ini] and place a
config file of your own at `rootston.ini` in the working directory (or in an
arbitrary location via `rootston -C`). Other options are available, refer to
`rootston -h`.

[rootston.ini]: https://github.com/swaywm/wlroots/blob/master/rootston/rootston.ini.example
