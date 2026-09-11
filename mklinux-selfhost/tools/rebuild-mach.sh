#!/bin/bash
# Run inside MkLinux after extracting dist/osfmk.tar.gz under /usr/src.
set -eu
export PATH="$PATH:/opt/ode/bin:/usr/src/osfmk/tools/ppc/ppc_linux/hostbin"
export USER=${LOGNAME:-root}
root=/usr/src/osfmk
kernel=$root/obj/powermac/mach_kernel/PRODUCTION/Mach_Kernel
grep -q 'Reserve the complete loaded segment' "$root/src/mach_kernel/ppc/ppc_init.c"
if [ -f /root/.sandboxrc ]; then
    cmp "$root/sandboxrc" /root/.sandboxrc
else
    cp "$root/sandboxrc" /root/.sandboxrc
fi
cp "$root/build_world" /usr/src/build_world
chmod +x /usr/src/build_world
echo "=== started $(date) ==="
touch /tmp/mach-rebuild-start
cd "$root/src"
echo '../../build_world' | nice -n 15 workon -sb osfmk
test "$kernel" -nt /tmp/mach-rebuild-start
if strings "$kernel" | grep -q PROBE; then
    echo 'ERROR: diagnostic probes remain in the kernel'
    exit 1
fi
ls -l "$kernel"
sync
echo "MACH_BUILD_FINISHED $(date)"
