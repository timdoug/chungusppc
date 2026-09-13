# Cordyceps bring-up: where it stands

Companion to [cordyceps.md](cordyceps.md), which has the register-level detail.
This file is for picking the work back up: what runs today, what does not, and
which roads are already known to be dead ends.

## State

`pm5200` and `pm6200` boot the ROM to the Mac OS "insert disk" screen with a
working mouse, boot Mac OS from an IDE image, show the MkLinux booter's dialog,
start the Mach microkernel, and run Linux 2.0.33-osfmach3 far enough to
calibrate its delay loop, size memory, bring up the network stacks and
initialise sound. It stops while reading the disk's partition descriptor.

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

## The blocker

Linux stops in `mac_label.c`, between `Reading descriptor` and
`Re-reading descriptor`, on a read that never completes. The last command the
disk sees is a RECALIBRATE.

The immediate cause is in the port, not the emulator. MkLinux's `wdc` driver
picks its ATA register spacing from `powermac_info.class` and handles PERFORMA
correctly, but the ATAPI accessors beside it test only for
`POWERMAC_CLASS_POWERBOOK` and so use the sixteen byte spacing on a Performa -
writing Device/Head to `0x50F1A060` and commands to `0x50F1A070`, which nothing
decodes. The disassembly is in [cordyceps.md](cordyceps.md#where-the-performa-kernel-gets-to).
The practical consequence is that after `wdc` probes the absent device 1, the
ATAPI probe's deselect never lands.

Two things to work out from there: whether the RECALIBRATE interrupt is reaching
Mach at all (it goes `IntSrc::IDE0` → `F108_INT_IDE0` → the VIA2 slot register →
CPU, and Mach's `performa_interrupt_initialize` never writes VIA2's IER), and
whether the ATAPI spacing wants patching out of the kernel the way
`valkyrie_probe` did. The kernel's own
`Generated fake interrupt to fix IDE hang.` string suggests lost IDE interrupts
were a known problem on this hardware.

Worth knowing: the family was supported but barely tested - the README says only
the 6214 was ever tried. Expect more holes. `floppy/grcswimiiihal.c` routes
floppy access through AMIC DMA calls that do not apply here, and
`PERFORMA_CUDA_BASE_PHYS` is `0x50F16000`, the floppy base on the PDM machines.

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
