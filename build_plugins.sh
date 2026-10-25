#!/bin/bash

# script to build VCOM with plugin support
# this requires prereqisites to be installed to a known prefix!

set -e

prefix="${1:-/opt/vzlu}"
meson setup --prefix "$prefix" --libdir lib --pkg-config-path "$prefix/lib/pkgconfig" -Dcommands_type="plugins" build
cd build && ninja
