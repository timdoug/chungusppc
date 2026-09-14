# Cordyceps bring-up: where it stands

Companion to [cordyceps.md](cordyceps.md), which has the register-level detail.
This file is for picking the work back up: what runs today, what does not, and
which roads are already known to be dead ends.

## State

`pm5200` and `pm6200` boot the ROM to the Mac OS "insert disk" screen with a
working mouse, boot Mac OS from an IDE image, hand over through the MkLinux
booter, and reach a **MkLinux DR3 login prompt in about two minutes**, in
colour, with the keyboard working. You can now **log in** (root / dingusppc) and
get a shell. A SCSI CD-ROM can be attached without upsetting any of it, and from
the shell **it mounts** - `/dev/scd0` as iso9660, contents readable. Nothing is
known to be broken; what is left is speed, and an install.

Note what that does *not* include. The `mklinux.img` this boots was installed on
a `pm7200` in an earlier session and simply carried across - MkLinux above Mach
is portable enough that it came up. **Nothing has ever been installed on this
machine**. See [Installing on a 6200CD](#installing-on-a-6200cd).

The root login on `mklinux.img` is **`root` / `dingusppc`** (DES `crypt`, so only
the first eight characters `dinguspp` are actually checked; the hash in
`/etc/passwd` is `CEBKNA7IFnXxE`). That was not written down before and is what
had kept the login prompt from ever being got past.

**Run the Performa kernel.** Everything above depends on it - see
[Which kernel](#which-kernel).

## Running it

```
./build/bin/chungusppc.app/Contents/MacOS/chungusppc \
    -b "63ABFD3F - Power Mac & Performa 5200,5300,6200,6300.ROM" -m pm6200 \
    --hdd_img macos-performa.img \
    --scsi_hdd_img macos-performa.img:mklinux.img --rambank1_size 32
```

Three things that are not optional:

* **`--rambank1_size 32`.** The 8 MB default runs off the end of memory partway
  through the Mac OS boot, logging accesses to `0x00800000`, and dies.
* **The MkLinux volume has to be the second SCSI disk.** Its booter is set to
  `/dev/sdb2`, and MkLinux names disks in discovery order rather than by ID, so
  a disk on its own is `sda` whichever ID it sits at. Hence the Mac OS volume
  first in `--scsi_hdd_img`, even though the machine boots Mac OS from IDE.
* **`--hdd_img` is the IDE disk.** `ScsiBus` no longer takes it, so SCSI disks
  go in `--scsi_hdd_img` and a second IDE disk in `--hdd2_img`.

Add `--cdr_img "MkLinux R2 RC5.toast"` for a CD; it attaches at the first free
ID from 3 upwards and says so in the log.

## Which kernel

MkLinux added support for the 52xx/53xx/62xx/63xx family on **31 July 2000**, and
the R2 disc ships the result:

```
MkLinux R2 RC5.toast
└── MkLinux-install/Place in Extensions Folder/
    ├── Mach Kernel                    1350052   generic, no Performa support
    └── Performas Use This!/
        ├── Mach Kernel                1612796   built 5 Aug 2000 - use this one
        └── README-PERFORMA               2750   the announcement and its caveats
```

Put that kernel in the System Folder's Extensions folder. It is bigger than
what it replaces, so it cannot be dropped into a disk image by overwriting the
old fork; copy it in the guest, or reallocate.

Anything older walks into bugs that were fixed by that work - most immediately
`valkyrie_probe()` calling `kmem_alloc()` from `initialize_screen()`, long
before the VM system exists, which lands in `zalloc(NULL)` and branches through
a 68k exception vector into ROM. That includes **anything rebuilt from the
retained 12/24/1999 osfmk snapshot**, which predates the Performa port entirely;
[`valkyrie-probe-performa.patch`](../../mklinux-selfhost/fixed-src/valkyrie-probe-performa.patch)
fixes that one instance if you must build from those sources.

## What had to be fixed

Machine-specific:

| Commit | Fix |
| --- | --- |
| `dd045e9a` | Capella interrupts on *changes* to the 68k priority level, including the fall back to idle. Rising-edge only wedges the first VBL forever. |
| `87594eea` | Valkyrie register 5 is the readable interrupt status; the ROM's vertical-blank wait spins on bit 0 of it. |

Shared code, all of it reached through this machine but none of it specific to
it:

| Commit | Fix |
| --- | --- |
| `04eda969` | 32-bit PIO on the ATA data port moves two words, and READ/WRITE BUFFER exist. Apple's ATA manager will not use a drive without them. |
| `37122355` | `Sc53C94::sequencer()` no longer dereferences a null DMA channel. |
| `22b6eea5` | Terminal count comes from the SCSI bus side when there is no DMA engine, or the driver and the chip deadlock waiting for each other. |
| `bdbdcd01` | A second IDE disk, `hdd2_img` / `hdd2_config`. |
| `0b56394f` | `hdd_img` belongs to the IDE bus on machines that have one, instead of being attached to both. |
| `060dceba` | `scsi_hdd_img`, so those machines can still have SCSI disks. |
| `05e0344f` | Screenshots convert the framebuffer again instead of reading back the locked SDL texture, which is write-only on Metal. |
| `5a0f9124` | A long access to the SCSI handshake port moves two words. Returning one lost half of every transfer, and Mac OS spun in `SyncWait` forever. |
| `c3528e54` | DATA_OUT through the handshake port: the sequencer waits in `SEND_DATA` and pushes each FIFO load to the target instead of overflowing the FIFO. |
| `35f26ca0` | An absent device 1 reads as zeroes when device 0 is present, instead of the `0xFF7F` that means an empty channel and looks like a ready drive. |
| `6b9709dd` | A software reset leaves device 0 selected, so a driver that resets to recover gets its disk back. |

Machine-specific, all of it needed before the drive interrupt reached MkLinux:

| Commit | Fix |
| --- | --- |
| `987c2f38` | VIA2 IFR writes clear a flag without needing bit 7; the slot cascade is not gated on a per-slot enable nothing writes; Valkyrie's vertical blank is on the slot register's video line, not the F108's. |
| `155f6988` | Writing a bit to `0x50F1A100` dismisses that F108 flag, which is how MkLinux's handler does it. |
| `ce469db0` | The Valkyrie palette answers at `+8` as well as `+4`, which is where MkLinux writes it. |
| `5434df42` | CD-ROMs say which SCSI ID they landed at, the way hard disks already did. |

## Installing on a 6200CD

Nothing here has been tried. It is the obvious next milestone and these are the
things worth knowing before spending hours on it.

**Install R2, not DR3.** The only Mach kernel that runs on this family is the
one dated 5 August 2000, and the R2 disc is what ships it. DR3's installer would
lay down a DR3 kernel with no Performa support at all, leaving you to swap it
afterwards - and that swap is the awkward one, because the Performa kernel is
larger than what it replaces and will not fit the fork in place.

**The installer runs under the kernel you already have.** This is the part that
makes the whole thing tractable: the booter loads `Mach Kernel` from the Mac OS
System Folder's Extensions, *not* from the CD. Leave the Performa kernel there,
point `lilo.conf` at the CD, and the installer runs on a kernel that supports
this hardware even though the disc's own kernel does not.

A sketch of the path, with the parts that are already established:

1. **Attach the disc.** `--cdr_img "MkLinux R2 RC5.toast"` lands it at SCSI ID 3
   - which is where DR3's stock `rootdev=/dev/scd0` expects a CD - and the
   machine still boots to a login prompt with one present. Verified.
2. **Give the target disk an Apple partition map.** The installer only runs
   `pdisk` if block 0 already holds the `ER` Driver Descriptor Record; without
   it, it silently runs `fdisk`, which MkLinux cannot use. Bootstrap the map
   once with the Mac OS `pdisk` in the disc's `MacOS Utilities`.
3. **Point the booter at the CD.** `rootdev=/dev/scd0` in `lilo.conf`, which the
   MkLinux control panel edits under **Custom...**. You can also write it from
   the host: it is a plain text file in the System Folder's Preferences, and
   `mklinux-selfhost/debug/hfs-list.py` will show you the current contents.
4. **Run the installer.** R2's is Red Hat's `newt` one and has its own quirks -
   choose fdisk rather than Disk Druid, set the root mount point with **F3** -
   all of which are written up under "MkLinux R2" in
   [the user guide](../users/mklinux.md).
5. **Afterwards**, set `rootdev` to the installed partition and check that the
   Performa kernel is still the one in Extensions.

Two things to expect:

* **It will take hours.** At ~150 KB/s an R2 install moves several hundred
  megabytes. Do not kill the emulator partway: a dirty root filesystem costs a
  full `fsck` on the next boot, which is slower still. Repair it from the host
  instead (below).
* **Writes are the least-tested path in the emulator**, though less so than
  before: a `dd` of 2 MB from `/dev/zero` to the SCSI root disk, synced, went
  through in 1.6 s (~1.3 MB/s) with no hang or error, so DATA_OUT through the
  handshake port (`c3528e54`) survives bulk writes. An install writes hundreds
  of megabytes over many small files, which is still heavier than anything tried,
  so it remains the first place to look if it goes wrong.

Unknowns worth settling cheaply before committing to a long run - both now
**settled**, from a root shell on the existing image with the disc attached:

* **MkLinux mounts the CD.** `mount -t iso9660 -o ro /dev/scd0 /mnt` succeeds and
  its contents read back fine - including `MkLinux-install/Place in Extensions
  Folder/`, whose `Mach Kernel` (1350052) and `Performas Use This!/Mach Kernel`
  (1612796) match the sizes in [Which kernel](#which-kernel) to the byte.
* **The SCSI probe copes with three targets.** `/proc/scsi/scsi` lists both
  Emulated Disks (ID 0, 1) and the `SONY CD-ROM CDU-8003A` (ID 3) cleanly; the
  absent-IDE-device-1 wedge that used to break the scan is fixed and does not
  return with a third target on the bus.

The disc is a hybrid: alongside `MkLinux-install/` it carries yaboot and a
`vmlinux-2.2.26mk` for LinuxPPC, which are not part of the MkLinux path.

## What is left: speed

This machine manages about **150 KB/s** over SCSI where a 6100 over AMIC DMA manages **6.9 MB/s**.
MkLinux moves every byte through the FIFO register with roughly three chip
commands each, having no pseudo-DMA path, which is what its own README means by
`SCSI is working, but rather slow... partially a lack of pseudo-DMA code`. A
clean boot takes about two minutes; anything heavier is correspondingly slow.

If the guest's root filesystem is left dirty - which killing the emulator does -
the next boot forces an `fsck` that takes far longer than the boot. The host's
`e2fsck` will repair it in place through the offset syntax:

```
e2fsck -fy 'mklinux.img?offset=32768'
```

where `32768` is the `Apple_UNIX_SVR2` partition's start block times 512.

## Do not repeat these

* **Do not alias the ROM at `0x40800000`.** It is tempting: the guest maps that
  range 1:1 and the 68k side addresses ROM from there. But nothing ever reads
  physical `0x408xxxxx` as data - the nanokernel maps the 68k ROM onto
  `0x40000000` through the page tables - and adding the alias only turns a
  precise abort into a silent infinite loop executing 68k bytes as PowerPC.
* **`--hold-keys` was tried and reverted** (`5a7e99fd`). Keys named on the
  command line do reach the guest and show up in the ADB keyboard's register 0,
  but Mac OS does not act on them at startup: `SPACE` does not bring up
  Extensions Manager, and whatever `Shift` does stops the machine four seconds
  in rather than producing an extensions-off boot. The guest never reads the
  keyboard's register 2, so the missing modifier bitmap there is not the gap.
* **Asserting DRQ from `rcv_data()` does not help the SCSI driver.** It is not
  watching that bit; it waits on terminal count in the status register.
* **Screenshots taken before `05e0344f` are worthless.** Every one of them is
  uniformly black on every machine.
* **Do not patch the guest kernel.** This one is known to have booted a real
  6214, so anything it does that looks wrong is worth understanding rather than
  editing - twice now the answer has been a bug on our side. Its ATAPI probe
  writing to `0x50F1A060`/`0x50F1A070`, which nothing decodes, is the current
  example: harmless, and harmless on real hardware too. (The `valkyrie_probe`
  patch is the exception that proves it: that one is only for kernels built
  from sources older than the port itself.)
* **Do not make a non-DMA SCSI `TRANSFER` fill the FIFO.** It triples this
  machine's throughput and stops the 6100 booting at all - Mac OS's SCSI
  Manager depends on getting exactly one byte per non-DMA transfer.
* **`mach_options=-r` does not give you a serial console.** The option reaches
  the kernel - the booter's dialog shows `Mach Options: -r`, and you set it by
  adding that line to `lilo.conf`, which the MkLinux control panel edits under
  **Custom...** - but `parse_args()` switches the console to the SCC before
  `initialize_serial()` has filled in `scc_softc`. The first `printf` then hangs
  in `scc_putc()` waiting for `SCC_RR0_TX_EMPTY` from a channel whose register
  pointer is null. Use the video console; it works.
* **Check which kernel a disk image is carrying before believing anything.**
  The version string is in the data fork - `Mach 3.0 VERSION(...)` with a build
  date - and the file is a 32-byte `MACH_BOOT_IMAGE` header, the kernel ELF,
  then the bootstrap task's ELF. `macos.img` carries the local rebuild, which
  is from sources that predate the Performa port; `macos-performa.img` carries
  the 5 August 2000 one.
* **Mach kernel addresses** map into that file as `addr - 0x200000 + 0x10010`
  for text. The built-in debugger will disassemble the running kernel by
  address, which is easier than carving the file, and it is the only PowerPC
  disassembler to hand - the vendored capstone is built without the PPC
  backend.

## Tooling notes

* `kill -USR1` writes `chungusppc-screen.bmp`. Take the PID from `$!` - `pgrep
  -f chungusppc` also matches the shell that launched it and will signal that
  instead.
* `kill -USR2` replays `chungusppc-input.txt`, which drives the keyboard and
  mouse. See the comment at the top of `core/hostevents_sdl.cpp` for the verbs.
* The debugger reads stdin at any time; feed it through a FIFO after a `sleep`
  so the machine has booted. `context 68k` switches to the 68k view, and
  addresses need an explicit `0x`.
* ROM file offsets: subtract `0x40800000` from a 68k address, `0x40000000` from
  a PowerPC one. Both halves live in the same 4 MB image.
* `--log-to-stderr` sends the log to stderr; the machine settings dump goes to
  stdout, so capture both when you want to confirm what was attached.
* **The debugger throws away the first line after a `SIGINT`.** It assumes the
  interrupt landed mid-typing, so a scripted `kill -INT; echo regs` never
  answers. Send a throwaway command first.
* **`pgrep`/`ps` matching on `chungusppc` also matches the shell that launched
  it.** Match the command on `MacOS/chungusppc$`, or have the launcher write its
  own PID.
* Reading files out of the guest's HFS volume from the host is the fastest way
  to check what the booter is actually being told. The catalog walk in
  `mklinux-selfhost/debug/hfs-list.py` prints `lilo.conf` and `MkLinux.prefs`
  outright.
