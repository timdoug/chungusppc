#!/bin/bash
cd /Users/timdoug/chungusppc
exec ./build/bin/chungusppc.app/Contents/MacOS/chungusppc \
  -b "/Users/timdoug/chungusppc/63ABFD3F - Power Mac & Performa 5200,5300,6200,6300.ROM" \
  -m pm6200 \
  --hdd_img /Users/timdoug/chungusppc/macos.img \
  --scsi_hdd_img /Users/timdoug/chungusppc/macos.img:/Users/timdoug/chungusppc/mklinux-r2.img \
  --rambank1_size 32 --log-to-stderr \
  --enet_backend=slirp --enet_hostfwd=tcp:2325:23
