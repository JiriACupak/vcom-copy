#!/bin/bash

set -e

prefix="${1:-/opt/vzlu}"
src_folder="${2:-/tmp/vzlu}"
vcom_folder=$PWD
SUDO=""
if [ "$UID" -ne 0 ]; then
       if [ -e "$prefix" ]; then
	       [ ! -w "$prefix" ] && SUDO="sudo -s"
       else
	       [ ! -w "$(dirname "$prefix")" ] && SUDO="sudo -s"
       fi
fi
echo "======================================"
echo "Installing VCOM into $prefix ..."
mkdir -p $src_folder
cd $src_folder
echo "======================================"
echo "Downloading dependencies into $src_folder ..."

echo "======================================"
echo "Installing LibCSP..."
git clone https://gitlab.com/vzlu/sw/libcsp.git
cd libcsp
git checkout v1.6.3
meson setup --prefix "$prefix" --libdir lib --pkg-config-path "$prefix/lib/pkgconfig" \
  -Dcsp_use_ifc_kiss=true -Dcsp_usart_linux=true -Dcsp_use_ifc_can=true -Dcsp_can_socketcan=true \
  -Dcsp_have_libsocketcan=true build
cd build
cmd='ninja install'
$SUDO eval "$cmd"
cd ../..

echo "======================================"
echo "Installing LibConsole..."
git clone https://gitlab.com/vzlu/sw/lib-console.git
cd lib-console
git checkout v0.3.1
meson setup --prefix "$prefix" --libdir lib --pkg-config-path "$prefix/lib/pkgconfig" -Duse_csp_commands=true -Dhave_csp=true build
cd build
cmd='ninja install'
$SUDO eval "$cmd"
cd ../..

echo "======================================"
echo "Installing lpldgen..."
# on x86_64 architecture, use pre-built CI artifact from lpldgen repo
# for anything else, we have to build from source
arch=$(uname -m)
echo $arch
if [[ $arch == x86_64* ]]; then
  echo "Using x86_64 binary relese ..."
  mkdir -p lpldgen
  cd lpldgen
  curl --output lpldgen.zip https://gitlab.com/-/project/48505992/uploads/0db43f8ffd7cc07370beeee84361f35f/lpldgen_v1.4.6.zip
  unzip lpldgen.zip
  cd pld
  cmd='./share/scripts/install.sh'
else
  echo "Building from source ..."
  git clone https://gitlab.com/vzlu/sw/lpldgen.git
  cd lpldgen
  git checkout v1.4.6
  meson setup --prefix "$prefix" --libdir lib --pkg-config-path "$prefix/lib/pkgconfig" build
  cd build
  cmd='ninja install && cd ../..'
fi
$SUDO eval "$cmd"

echo "======================================"
echo "Installing LibCommands..."
git clone https://gitlab.com/vzlu/sw/libcommands.git
cd libcommands
git checkout v0.1.2
meson setup --prefix "$prefix" --libdir lib --pkg-config-path "$prefix/lib/pkgconfig" build
cd build
cmd='ninja install'
$SUDO eval "$cmd"
cd ../..

echo "======================================"
echo "Installing SALLY..."
git clone https://gitlab.com/vzlu/sw/sally.git
cd sally
git checkout v0.3.0
meson setup --prefix "$prefix" --libdir lib --pkg-config-path "$prefix/lib/pkgconfig" -Denable_csp=true build
cd build
cmd='ninja install'
$SUDO eval "$cmd"
cd ../..

echo "======================================"
echo "Building VCOM (with plugins support) ..."
cd $vcom_folder
./build_plugins.sh

# cleanup
if [[ $# -lt 2 ]]; then
  rm -rf $src_folder
fi
