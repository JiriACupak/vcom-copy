#!/bin/sh

set -e

if [ $# -lt 2 ] ; then
  ifc="can0"
  bitrate=1000000
  echo "Usage: caninit.sh <interface name> <bitrate>"
  echo "Assuming default: caninit.sh $ifc $bitrate"
else
  ifc=$1
  bitrate=$2
fi

ip link set dev $ifc down
ip link set dev $ifc up type can bitrate $bitrate
ip link set dev $ifc txqueuelen 1000

echo "Init done!"
