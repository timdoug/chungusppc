# Cordyceps bring-up: where it stands

Companion to [cordyceps.md](cordyceps.md), which has the register-level detail.
This file is for picking the work back up: what runs today, what does not, and
which roads are already known to be dead ends.

## State

`pm5200` and `pm6200` boot the ROM to the Mac OS "insert disk" screen with a
working mouse, boot Mac OS from an IDE image, show the MkLinux booter's dialog,
and start the Mach microkernel, which prints its banner and its memory map to
the Valkyrie console and stops after `vm_page_bootstrap`. That is the blocker,
and it now needs one patch applied to the guest's kernel to reach - see
[Patch the guest kernel](#patch-the-guest-kernel).

## Running it

```
./build/bin/chungusppc.app/Contents/MacOS/chungusppc \
    -b "63ABFD3F - Power Mac & Performa 5200,5300,6200,6300.ROM" -m pm6200 \
    --hdd_img macos.img --scsi_hdd_img macos.img:mklinux.img --rambank1_size 32
```

Three things that are not optional:

* **`--rambank1_size 32`.** The 8 MB default runs off the end of memory partway
  through the Mac OS boot, logging accesses to `0x00800000`, and dies.
* **The MkLinux volume has to be the second SCSI disk.** Its booter is set to
  `/dev/sdb2`, and MkLinux names disks in discovery order rather than by ID, so
  a disk on its own is `sda` whichever ID it sits at. Hence `macos.img` first in
  `--scsi_hdd_img`, even though the machine boots Mac OS from IDE.
* **`--hdd_img` is the IDE disk.** `ScsiBus` no longer takes it, so SCSI disks
  go in `--scsi_hdd_img` and a second IDE disk in `--hdd2_img`.

## Patch the guest kernel

MkLinux's `valkyrie_probe()` calls `kmem_alloc()` from `initialize_screen()`,
which `go()` runs long before the VM system exists, and the resulting
`zalloc(NULL)` branches through a 68k exception vector into ROM. The fix is in
[`valkyrie-probe-performa.patch`](../../mklinux-selfhost/fixed-src/valkyrie-probe-performa.patch);
the reasoning is in [cordyceps.md](cordyceps.md#the-branch-to-0x40802xxx).

Rebuilding the kernel is the real answer. To try it without one, patch the
`Mach Kernel` file in place: the PERFORMA arm of `valkyrie_probe` is at kernel
address `0x0027E5D4`, and turning the `cmpwi`/`bne` pair that guards the
allocation into `li r3,1` / `b` to the epilogue skips it.

```python
old = bytes.fromhex('38893978' '900A397C' '2C0B0000' '4082002C' '3D200041')
new = bytes.fromhex('38893978' '900A397C' '38600001' '480000B4' '3D200041')
```

That byte string occurs once in the disk image. Addresses are for the Mach
kernel built 2026-09-11; check the disassembly before trusting them on another
build.

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

## The blocker

Mach stops after `vm_page_bootstrap: 7418 free pages`. Sampling the processor
shows it alternating between `0x300`, the DSI vector, and `0x2024`/`0x206C` in
the exception entry code mapped at `0x2000`: the exception path is faulting on
itself, at `0x263A68`, `lwz r3,0x14C(r3)`, where it picks a thread's kernel
stack out of `per_proc_info`. `lr` is `0x2664B0`, immediately after an indirect
call through the interrupt dispatch vector - the arm that panics with
`Unsupported class for interrupt dispatch` is the other one, so the Performa
dispatch is installed and being entered.

`interrupt_performa.c` is the file to read first: it wants Capella at
`PERFORMA_CAPELLA_BASE_PHYS`, acknowledges through `CAPELLA_INT_REG_OFFSET`,
writes `PERFORMA_ICR`, and cascades VIA1/VIA2/F108. Check those addresses
against what `PrimeTimeTwo` and `F108` actually decode, and check that the
interrupt is not arriving before Mach has a stack to take it on.

Worth knowing: MkLinux's Performa port is real but Apple never validated it, and
the `valkyrie_probe` bug above proves the class was never run. Expect more holes
in the port, not just in the emulator. `floppy/grcswimiiihal.c` routes floppy
access through AMIC DMA calls that do not apply here, and
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
* **The Mach kernel in `macos.img` is the local rebuild**, not DR3's. Its
  version string dates it, and addresses quoted here are from that build.

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
