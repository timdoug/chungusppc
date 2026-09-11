#!/bin/bash
# Install only a successfully built 2.0.40 server; preserve the active server.
set -e
cd /usr/src/mklinux-2.0.40/src
test -s vmlinux
test -s System.map
strings vmlinux | grep '^Linux version 2.0.40-osfmach3 '
grep -q 'BUILD_FINISHED' /tmp/build-server-2.0.40.log
if [ ! -f /mach_servers/vmlinux.before-2.0.40 ]; then
    cp -p /mach_servers/vmlinux /mach_servers/vmlinux.before-2.0.40
fi
if [ ! -f /mach_servers/System.map.before-2.0.40 ]; then
    cp -p /mach_servers/System.map /mach_servers/System.map.before-2.0.40
fi
# The matching rollback map is already installed; make clean removes the
# historical source tree's copy. Preserve it if upgrading another old guest.
if [ ! -f /mach_servers/System.map-2.0.38-osfmach3 ] && \
   [ -f /usr/src/mklinux/src/System.map ]; then
    cp -p /usr/src/mklinux/src/System.map /mach_servers/System.map-2.0.38-osfmach3
fi
make modules_install
cp -p vmlinux /mach_servers/vmlinux-2.0.40-osfmach3
cp -p System.map /mach_servers/System.map-2.0.40-osfmach3
cp -p .config /mach_servers/config-2.0.40-osfmach3
cp -p vmlinux /mach_servers/vmlinux.new
cmp vmlinux /mach_servers/vmlinux.new
mv -f /mach_servers/vmlinux.new /mach_servers/vmlinux
cp -p System.map /mach_servers/System.map
sync
printf 'INSTALLED '; date
ls -l /mach_servers/vmlinux /mach_servers/vmlinux.before-2.0.40
