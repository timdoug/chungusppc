# Rebuilding Mach and MkLinux 2.0.40

Build the OSF Mach microkernel with its read-only memory reservation fix and
quiet floppy driver, plus the MkLinux server ported to Linux `2.0.40-osfmach3`.
This directory contains
the patches, configuration, source checksums, build tools, and regression tests.
Original compressed archives in `sources/` allow offline reproduction and are
ignored by Git. Extracted host trees and transfer archives are disposable.

## Prepare on the host

From the ChungusPPC repository root, with Python 3 and Git installed:

```sh
python3 mklinux-selfhost/prepare-sources.py --check
python3 mklinux-selfhost/prepare-sources.py
```

The first command verifies the inputs, applies the patches in temporary
directories, and compares the output to the boot-tested sources. The second
also writes `dist/osfmk.tar.gz` and `dist/mklinux-2.0.40.tar.gz`, including the
build scripts and configuration. Either accepts `mach` or `linux` to prepare
just one target. Temporary extraction trees are removed automatically.
The transfer archives use GNU tar format for the guest's old tar.

Serve them to the running guest:

```sh
python3 -m http.server 8089 --bind 127.0.0.1 --directory mklinux-selfhost/dist
```

The guest can fetch from `http://10.0.2.2:8089/` with the slirp network backend.
Stop the HTTP server after transfer. Delete `dist/` when finished; it can always
be regenerated. These steps reproduce sources and build settings; embedded
build dates and build numbers will change on a later native build.

## Native build prerequisites

A MkLinux R2 installation needs a development environment and these packages from
`MkLinux R2 RC5.toast`, under `/RedHat/RPMS`:

| Package | Purpose |
| --- | --- |
| `gcc-2.95.4-4j.ppc.rpm` | Compiler used for both validated builds |
| `binutils-2.13.90.0.2-2.ppc.rpm` | Assembler and linker |
| `osfmk-export-12.24.99-2.ppc.rpm` | Mach headers and libraries |
| `osfmk-tools-12.24.99-2.ppc.rpm` | `mig`, `migcom`, `config`, `makeboot` |
| `ode-2.3.4-5f.ppc.rpm` | Mach's OSF build harness |

The source archive also contains the Mach tools. `build_world` and `sandboxrc`
were recovered separately from
`https://ftp.mklinux.org/pub/mklinux-pre-R1/SRPMS/sources/`; the retained harness
adds `set -e` so a failed stage stops the build. Linux can use the prebuilt Mach
exports without rebuilding Mach first.

## Build Mach

In the guest as root:

```sh
cd /tmp
wget -O osfmk-fixed.tar.gz http://10.0.2.2:8089/osfmk.tar.gz
cd /usr/src
tar xzmf /tmp/osfmk-fixed.tar.gz
( setsid bash /usr/src/osfmk/rebuild-mach.sh > /tmp/rebuild-mach.log 2>&1 < /dev/null & )
```

The script checks the fixed source, installs the recovered ODE harness, and
builds `PRODUCTION`. It checks an existing `/root/.sandboxrc` rather than
overwriting a different sandbox configuration. The final log marker is
`MACH_BUILD_FINISHED`. The result is:

```
/usr/src/osfmk/obj/powermac/mach_kernel/PRODUCTION/Mach_Kernel
```

It contains the kernel and bootstrap, with no diagnostic probes. Installation
is separate: copy this image to the Mac boot volume's
`System Folder:Extensions:Mach Kernel` data fork. If using host hfsutils, shut
the guest down cleanly and stop ChungusPPC before modifying `macos.img`:

```sh
hmount macos.img
hcopy -r /path/to/Mach_Kernel ':System Folder:Extensions:Mach Kernel'
humount
```

GNU ld can place `.rodata` and `.sdata2` after `etext` in the executable load
segment. The Mach patch reserves that complete segment so its allocator cannot
reuse memory containing live kernel constants and dispatch tables.

The floppy patch puts the per-interrupt `HALISR` and sector-lookup
`HALGetNextAddr`/`DMASTATUS` traces behind `MACH_DEBUG`, like the driver's
other debug output. `PRODUCTION` skips these prints and diagnostic register
reads. Error reporting remains enabled.

## Build Linux 2.0.40

In the guest as root:

```sh
cd /tmp
wget -O mklinux-2.0.40-source.tar.gz http://10.0.2.2:8089/mklinux-2.0.40.tar.gz
cd /usr/src
tar xzmf /tmp/mklinux-2.0.40-source.tar.gz
cd /usr/src/mklinux-2.0.40/src
md5sum -c ../source-md5sums.txt
( setsid nice -n 15 bash ../build-server.sh > /tmp/build-server-2.0.40.log 2>&1 < /dev/null & )
```

The build uses the retained `server.config`, including ISO9660/NLS, AWACS sound and PPP
modules. Look for `BUILD_FINISHED` and `BUILD_EXIT=0` before installing:

```sh
bash /usr/src/mklinux-2.0.40/install-server.sh
```

This preserves the previous server, installs matching modules, and replaces
`/mach_servers/vmlinux`. Reboot after installation. For an incremental rebuild,
use `resume-build.sh` with the same log redirection. Regression checks after boot:

```sh
cd /usr/src/mklinux-2.0.40
gcc -O2 -Wall runtime-check.c -o /tmp/runtime-check-2.0.40
bash run-runtime-checks.sh
```

See [PORTING.md](rebase-2.0.40/PORTING.md) for the integration details and
rollback procedure.

## Clean after building

After builds finish, run in the guest:

```sh
cd /usr/src/mklinux-2.0.40/src
make clean
rm -rf /usr/src/osfmk/obj/powermac
```

This keeps source, Linux configuration, installed kernels/modules, rollback
files, Mach exports, and the toolchain. ODE recreates its object directory on
the next build. Removing files inside the guest frees ext2 space; it does not
shrink the fixed-size host disk image.

## Restore the original source archives

`sources/manifest.json` pins both archives by SHA-256. Restore Linux on the host:

```sh
curl -fsSL -o mklinux-selfhost/sources/linux-2.0.40.tar.xz https://www.kernel.org/pub/linux/kernel/v2.0/linux-2.0.40.tar.xz
```

For Mach, `osfmk-src-12.24.99-2.ppc.rpm` on the retained R2 disc contains
`/usr/src/osfmk.tar.gz`. Copy that original archive to
`mklinux-selfhost/sources/osfmk.tar.gz`. It can also be recovered from the current
guest at that path. The identically named FTP download historically used a
different compression format; use the disc archive matching the saved checksum.
Run `prepare-sources.py --check` after restoring either input.
