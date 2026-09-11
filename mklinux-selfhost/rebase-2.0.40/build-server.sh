#!/bin/bash
# Run natively after extracting dist/mklinux-2.0.40.tar.gz under /usr/src.
set -e
export PATH=$PATH:/usr/src/osfmk/tools/ppc/ppc_linux/hostbin
export USER=root
cd /usr/src/mklinux-2.0.40/src
trap 'result=$?; echo "BUILD_EXIT=$result $(date)"; sync' EXIT
printf 'BUILD_STARTED '; date
mkdir -p export-osfmach3/osfmach3_ppc export-osfmach3/osfmach3_i386
cp ../server.config .config
make symlinks
sed 's|read ans </dev/tty|read ans|' scripts/Configure > /tmp/Configure-2.0.40.stdin
yes '' | bash /tmp/Configure-2.0.40.stdin -d arch/osfmach3_ppc/config.in > /tmp/config-2.0.40.log 2>&1
grep '^CONFIG_OSFMACH3=y$' .config
grep '^CONFIG_ISO9660_FS=y$' .config
grep '^CONFIG_NLS=y$' .config
if grep -q '^CONFIG_UNSAFE_MMAP=y$' .config; then exit 1; fi
printf 'CONFIG_DONE '; date
make dep > /tmp/dep-2.0.40.log 2>&1
printf 'DEPENDENCIES_DONE '; date
make vmlinux > /tmp/vmlinux-2.0.40.log 2>&1
make modules > /tmp/modules-2.0.40.log 2>&1
printf 'BUILD_FINISHED '; date
test -s vmlinux
strings vmlinux | grep '^Linux version 2.0.40-osfmach3 '
ls -l vmlinux System.map
sync
