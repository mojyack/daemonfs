#!/bin/zsh
set -e

workdir="$PWD/test"
rootfs="$workdir/mnt"

mkdir $rootfs/{e1d,e2d,e3d}
echo "$workdir/example1d.sh" > $rootfs/e1d/args
echo "$workdir/example2d.sh" > $rootfs/e2d/args
echo "$workdir/example3d.sh" > $rootfs/e3d/args
truncate -s 4096 $rootfs/e1d/stdout
truncate -s 4096 $rootfs/e1d/stderr
echo up > $rootfs/e1d/state
echo up > $rootfs/e2d/state
echo up > $rootfs/e3d/state
