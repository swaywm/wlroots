#!/usr/bin/env bash

# TODO add me to the build system

set -e

rm -rf xdg-shell
mkdir -p xdg-shell
wayland-scanner code /usr/share/wayland-protocols/unstable/xdg-shell/xdg-shell-unstable-v6.xml xdg-shell/xdg-shell.c
wayland-scanner server-header /usr/share/wayland-protocols/unstable/xdg-shell/xdg-shell-unstable-v6.xml xdg-shell/xdg-shell.h

