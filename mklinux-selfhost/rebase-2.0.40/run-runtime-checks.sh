#!/bin/bash
set -e
trap 'result=$?; echo "CHECKS_EXIT=$result $(date)"; sync' EXIT
uname -a
cat /proc/version
test "$(uname -r)" = 2.0.40-osfmach3
/tmp/runtime-check-2.0.40
if ! grep -q ' /mnt/cdrom ' /proc/mounts; then mount /mnt/cdrom; fi
test -s /mnt/cdrom/RedHat/RPMS/osfmk-src-12.24.99-2.ppc.rpm
ls -l /mnt/cdrom/RedHat/RPMS/osfmk-src-12.24.99-2.ppc.rpm
ping -c 3 -s 8 10.0.2.2
/sbin/depmod -a
/sbin/insmod /lib/modules/2.0.40-osfmach3/net/bsd_comp.o
/sbin/lsmod
/sbin/rmmod bsd_comp
/sbin/insmod /lib/modules/2.0.40-osfmach3/net/ppp_deflate.o
/sbin/lsmod
/sbin/rmmod ppp_deflate
date
date -u
sync
echo CHECKS_FINISHED
