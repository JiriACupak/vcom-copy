#!/bin/bash

# script to build statically linked VCOM with command support
# this builds VCOM as static binary, with the possibility to add statically linked commands as subprojects

set -e

meson setup -Dcommands_type="static" build
cd build && ninja
