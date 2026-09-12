# MkLinux on Linux 2.0.40

`mklinux-on-linux-2.0.40.patch` ports the December 24, 1999 MkLinux Linux-server
snapshot to Linux 2.0.40. It applies to pristine Linux 2.0.40 and includes the
MkLinux architecture support and the integration fixes described below.

The native build target is **osfmach3_ppc on Power Macintosh 7200 / PowerPC
601**, using GCC 2.95.4 and OSF Mach exports. The patch covers the whole source
tree; other architectures and optional drivers have not been runtime-tested.

## Reproduce

From the repository root:

```sh
python3 mklinux-selfhost/prepare-sources.py linux
```

The preparation script verifies the input archive, applies the patch, and checks
all 2,875 output source files against `source-manifest.json`. That manifest also
records the original upstream and MkLinux source provenance. The resulting
`../dist/mklinux-2.0.40.tar.gz` contains sources, the exact `server.config`, native
build/install scripts, and regression tests. Temporary extraction trees are
removed automatically. See [README.md](../README.md) for prerequisites, transfer,
build, installation, and cleanup commands.

## Integration details

* **ext2 and bext2:** preserve MkLinux's little-endian disk accessors and bitmap
  operations. The new ext2 directory format uses an 8-bit `name_len` and
  `file_type`, so `name_len` must no longer be byte-swapped. Convert cached
  superblock feature fields to host byte order. Preserve upstream group
  descriptor dirty-buffer and bitmap-cache fixes, including in the separate
  bext2 driver. Set `mknod`'s directory-entry type after allocating the entry,
  and initialize the type bytes for `.` and `..` in the older format.
* **fork and exec:** retain Mach identity-change notification, task references,
  and its explicit parent argument. Derive the new privilege counters from
  that parent. Reject user-supplied internal clone flags at the Mach syscall
  entry points while allowing internal server thread creation. Preserve
  upstream bounded argument counting, ELF interpreter checks, and the
  dumpable-race fix.
* **signals:** carry SIGURG's ignore-by-default behavior into both Mach
  architecture-specific signal handlers.
* **Ethernet:** zero-pad frames shorter than 60 bytes in Mach's network glue.
  The in-band device-write RPC copies the padded stack buffer; existing skb
  ownership and locking remain intact.
* **proc and formatting:** preserve Mach's separate proc/VM implementation
  while keeping `/proc/*/mem` mmap disabled. Build the bounded `_vsnprintf`
  entry point and retain the unbounded wrappers supplied by `libsa_mach`.
  Read `%h` arguments as promoted `int` before narrowing them, as required by
  PowerPC varargs and GCC 2.95.4.
* **build and devices:** use `KERNELRELEASE` with the `-osfmach3` suffix for
  modules, retain Mach headers/libraries, name the boot-parameter structure,
  and stop module compilation on a failed subdirectory. Keep ADB character
  major 56 and IDE block major 56 in their separate device namespaces.

## Regression tests

`runtime-check.c` covers fork/exec, oversized arguments, restricted clone
flags, SIGURG, proc memory mmap, Unix descriptor passing, ext2 allocation and
indirect I/O, truncate, rename, links, FIFOs, device nodes, and timestamp
permissions. `run-runtime-checks.sh` adds ISO9660, short-packet networking, and
PPP module load/unload checks.

`formatter-check.c` tests bounded string formatting and integer varargs using
the actual compiled kernel formatter. After building the server, run in the guest:

```sh
cd /usr/src/mklinux-2.0.40
gcc -O2 formatter-check.c src/lib/vsprintf.o src/lib/ctype.o -o /tmp/formatter-check
/tmp/formatter-check
```

## Rollback

The installer preserves the previous server at
`/mach_servers/vmlinux.before-2.0.40` and, when available, its matching source-built
map at `/mach_servers/System.map-2.0.38-osfmach3`. To restore them in the guest:

```sh
cp -p /mach_servers/vmlinux.before-2.0.40 /mach_servers/vmlinux.rollback
command mv -f /mach_servers/vmlinux.rollback /mach_servers/vmlinux
cp -p /mach_servers/System.map-2.0.38-osfmach3 /mach_servers/System.map
sync
shutdown -h now
```

Restart ChungusPPC after the guest finishes shutting down. The installed
2.0.38 module directory remains available for the previous server.
