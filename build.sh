#!/bin/bash

# script to build VCOM with default options and forcing subproject usage
# this builds VCOM as static binary without plugin or additional command support!

set -e

meson setup --force-fallback-for=commands,console,csp,pld,srv-abs build
cd build && ninja
