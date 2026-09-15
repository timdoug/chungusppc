#!/bin/bash
cd /Users/timdoug/chungusppc
exec ./build/bin/chungusppc.app/Contents/MacOS/chungusppc \
  -w /Users/timdoug/chungusppc/mklinux-selfhost/debug/pm7500 \
  -b "/Users/timdoug/chungusppc/9630C68B - Power Mac 7200&7500&8500&9500 v2.ROM" \
  -m pm7500 --rambank1_size 128 \
  --hdd_img macos.img:mklinux.img \
  --mon_id=VGA-SVGA --gfxmem_size=4 \
  --enet_backend=slirp --enet_hostfwd=tcp:2324:23
