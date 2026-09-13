# Cordyceps bring-up: where it stands

Companion to [cordyceps.md](cordyceps.md), which has the register-level detail.
This file is for picking the work back up: what runs today, what does not, and
which roads are already known to be dead ends.

## State

`pm5200` and `pm6200` boot the ROM to the Mac OS "insert disk" screen with a
working mouse, boot Mac OS from an IDE image, and hand off to the MkLinux booter
without crashing. The Mach kernel then starts and goes quiet: no console, no
video mode change, but the display refresh keeps ticking and the machine is not
in an exception storm. That is the blocker.

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

## The blocker

The booter hands off and the Mach kernel produces nothing. It is alive - timers
run, the display refresh ticks - so this is not the exception storm that an
earlier bug caused. It simply never gets far enough to print.

The obvious first move is **to make it talk**: MkLinux can be pointed at the
serial port for console output, and `--serial_backend stdio` puts that on the
terminal. Early Mach output would turn this from a blind hunt into a normal one.

Worth knowing before starting: MkLinux's Performa port is real but Apple never
validated it. `go.c` prints `Power Macintosh Performa (unsupported) class
machine`, and the family is absent from the DR3 release notes. Expect the port
itself to have holes, not just the emulator. `interrupt_performa.c` and
`video_valkyrie.c` are the files to read first; `floppy/grcswimiiihal.c` is
known to route floppy access through AMIC DMA calls that do not apply here.

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
