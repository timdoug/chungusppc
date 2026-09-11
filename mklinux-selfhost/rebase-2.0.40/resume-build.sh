#!/bin/bash
set -e
export PATH=$PATH:/usr/src/osfmk/tools/ppc/ppc_linux/hostbin
export USER=root
cd /usr/src/mklinux-2.0.40/src
trap 'result=$?; echo "BUILD_EXIT=$result $(date)"; sync' EXIT
printf 'BUILD_RESUMED '; date
make vmlinux > /tmp/vmlinux-2.0.40.log 2>&1
make modules > /tmp/modules-2.0.40.log 2>&1
printf 'BUILD_FINISHED '; date
test -s vmlinux
strings vmlinux | grep '^Linux version 2.0.40-osfmach3 '
ls -l vmlinux System.map
sync
