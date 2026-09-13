# Cordyceps (Power Macintosh 5200/6200)

The Power Macintosh and Performa 5200, 5300, 6200 and 6300 share one logic
board: a Quadra/LC 630 design with a PowerPC 603 grafted on. There is no NuBus,
no PCI and no DMA engine — the processor moves all I/O data itself. The ROM
identifies itself as `Boot Cordyceps 6` (checksum `0x63abfd3f`), which is where
the name used here comes from.

Two sources describe this hardware:

* the *Power Macintosh 5200/75 LC and 6200/75 Computers* developer note
  (Apple, 1995), for the block diagram, chip roles and address map;
* MkLinux's Mach kernel, which carries a `POWERMAC_CLASS_PERFORMA` port in
  `ppc/POWERMAC/powermac_performa.h`, `interrupt_performa.c` and the
  `CLASS_PERFORMA` branches of `wd.c`, `scsi_53C94_hdw.c`, `serial_io.c`,
  `cuda.c`, `video_valkyrie.c` and `floppy/grcswimiiihal.c`. This is the only
  register-level description of the machine we have. It is compiled into the
  shipped R2 kernels, but Apple never validated it: `go.c` prints
  `Power Macintosh Performa (unsupported) class machine`, and the family is
  absent from the DR3 release notes' machine list.

## Custom ICs

| IC | Role | Emulated by |
| --- | --- | --- |
| Capella | Bridges the 64-bit 603 bus to the 32-bit 68040 bus; L2 and ROM control | `F108` (register window only) |
| F108 | Memory control, plus SCSI (53C96-alike), SCC (8530-alike) and IDE | `F108` + `PrimeTimeTwo` decode |
| PrimeTime II | I/O bus bridge: VIA1, VIA2, SWIM II, interrupts, sound buffers | `PrimeTimeTwo` |
| DFAC II | Sound codec on the IIC bus | not emulated |
| Cuda | ADB, PRAM, real-time clock, soft power, IIC master | `ViaCuda` |
| Valkyrie | Display CLUT and DAC, 1 MB DRAM frame buffer, 4/8/16 bpp | `ValkyrieVideo` |

## Address map

RAM starts at zero as one contiguous block (8 to 64 MB across two 72-pin
SIMMs; nothing is soldered on the board). The 4 MB ROM lives on the ROM/cache
DIMM together with a 256 KB L2 cache.

| Range | Contents |
| --- | --- |
| `0x00000000` | RAM |
| `0x40000000` | 603 ROM space |
| `0x50F00000` | I/O page, decoded by PrimeTime II |
| `0x53000000` | Capella registers (interrupt acknowledge and priority level) |
| `0xF9000000` | display RAM |
| `0xFE000000` | PDS expansion card (NuBus slot `$E`) |
| `0xFFC00000` | ROM image the 603 starts from |

Within the I/O page:

| Offset | Device | Register stride |
| --- | --- | --- |
| `0x00000` | VIA1 (Cuda) | `0x200` |
| `0x02000` | VIA2 | `0x200` |
| `0x0C000` | SCC (channel B regs `+0`, A regs `+2`, B data `+4`, A data `+6`) | 2 |
| `0x10000` | SCSI 53C96; pseudo-DMA port at `+0x100` | 16 |
| `0x14000` | Apple Sound Chip: FIFO A `+0`, FIFO B `+0x400`, control `+0x800` | 1 |
| `0x16000` | SWIM II | `0x200` |
| `0x1A000` | IDE channel 0 (alternate status at `+0x38`) | 4 |
| `0x1A101` | F108 interrupt flags | — |
| `0x24000` | Valkyrie CLUT |  |
| `0x2A000` | Valkyrie control registers |  |

The same devices also answer at the raw `0x50000000` decode. The ROM's serial
output routine at `0x403079C4` drives the SCC through `0x5000C000`, which is
what MkLinux's "Really, it's 5000C000" comment refers to.

Capella's window carries more than the interrupt registers. The ROM's power-on
test at `0x403089CC` enables a mode through `+0x0C` and then walks the 256 KB
L2 cache on the ROM/cache DIMM through two diagnostic windows, `0x51000000`
(data, `0x3E800` bytes) and `0x52000000` (tags). Backing both with plain
storage passes the test; the emulator has no caches to keep coherent.

## Interrupts

There is no interrupt controller register of the kind AMIC provides; the VIAs
drive 68k-style autovector levels. The request towards the 603 is *latched*:
the ROM's handler at `0x40307358` acknowledges Capella at `+0x1C` and returns
without touching whatever interrupted, so a level-driven line re-enters the
handler forever. The latch re-arms when a new source asserts, and the request
also drops once the last source deasserts, as the priority lines do. (MkLinux
acknowledges at `+0x18` instead; both addresses are accepted.)

```
IDE0/IDE1, VBL -> F108 IFR -> VIA2 slot IFR bit 0 -> VIA2 IFR bit 1 -> level 2
SCSI, sound, floppy ------------------------------> VIA2 IFR --------> level 2
Cuda, 60.15 Hz tick, timers ----------------------> VIA1 ------------> level 1
SCC ---------------------------------------------------------------> level 4
```

The VIA2 slot flags use reverse logic (0 means asserted); the F108 flags are
cleared by writing a 1.

Capella `+0x24` reports the resulting 68k interrupt priority level in its low
three bits, **active low**, and the ROM passes it to the 68k emulator. All ones
means nothing is pending. Returning zero instead looks like level 7, so the
emulator takes an NMI before the 68k ROM has set up a stack pointer, pushes an
exception frame through the ROM checksum word it is still using as A7, faults,
and double-faults into the nanokernel panic.

## Debugging the ROM

The ROM is far more talkative than it looks, which makes bring-up tractable.

* **It has a serial monitor.** On a power-on self test failure it prints
  `\r\n>` on the modem port and waits for a command (`?` prints help). Run with
  `--serial_backend=stdio` to see it. Reaching this prompt means a POST
  subtest failed, not that the emulator crashed.
* **It records which test failed.** Each subtest is a `bl` followed by
  `and. r3, r14, r14`; a non-zero `r14` means failure, and the dispatcher at
  `0x40305080`-`0x40305340` ORs a bit into the word at physical `0x0004002C`
  before branching to the monitor. Dump that word to see how far POST got:

  | Flag | Set at | Flag | Set at |
  | --- | --- | --- | --- |
  | `0x00000002` | `0x40305754` | `0x00000800` | `0x403051ec` |
  | `0x00000008` | `0x40305738` | `0x00001000` | `0x40305280` |
  | `0x00000010` | `0x403050dc` | `0x00020000` | `0x403052b0` |
  | `0x00000020` | `0x40305124` | `0x08000000` | `0x4030521c` |
  | `0x00000040` | `0x403050a4` | `0x10000000` | `0x40305770` |
  | `0x00000100` | `0x40305170` | `0x00000400` | `0x403051bc` |

* **The 68k side is reachable too.** The emulator keeps the 68k PC in `r24`,
  the current opcode in `r27`, A7 in `r1`, and its context block at `r31` =
  `0x68FFF000` (A7 at `+0x4C`). Nanokernel services are `twi 31, r31, N`
  instructions in the table at `0x6806E680`. The 68k ROM is mapped at
  `0x40800000`, so 68k code seen at `0x408xxxxx` disassembles with
  `context 68k` and `disas N,0x400xxxxx` — the debugger reads physical
  addresses, and the ROM sits at `0x40000000`. The VIA interrupt dispatchers
  are at ROM `0x1C2F4` (VIA1, through low-memory global `0x1D4`) and `0x1C314`
  (VIA2, through `0xCEC`).
* **The nanokernel panic routine is at `0x40310D40`.** It saves the FPRs, then
  spins forever incrementing a counter at address 0 (`0x40310DD0`). Breaking
  there with `until 0x40310d40` and reading `lr`, `srr0` and `srr1` identifies
  what died: `srr1` bit 14 set means the 68k emulator executed a trap
  instruction.

## The machine ID

The ROM reads the ID at `0x5FFFFFFC` — twice as a longword, once as the low
byte, and once as a **16-bit word**, which is why `NubusMacID` had to learn
word reads. It then walks a table of machine descriptors: `0x1B7F0` loads the
table base `0x203DC`, and for each self-relative entry compares the ID against
the word at `+0x58` of the descriptor. Running off the end of the table
branches to `0x1B8A8`, a `bra.s *` that hangs the machine with no diagnostic
at all.

That table holds ten entries for this family: `0x3250`, `0x3251`, `0x3254`,
`0x3255`, `0x3256`, `0x3258`, `0x3259`, `0x325C`, `0x325D` and `0x325E`. The
low bytes agree with MkLinux's `model_dep.c`, which expects `0x50`/`0x58` on
75 MHz models and `0x51`/`0x59` on 80 MHz ones. Which entry corresponds to
which model is not yet known; `pm5200` and `pm6200` both default to `0x3250`
and can be pointed elsewhere with the `machine_id` property.

## Status and unverified guesses

POST passes, the 68k emulator runs, and the ROM programs Valkyrie for 640x480
at 4 bpp and 66.6 Hz and fills the frame buffer with the 50% desktop dither.
It then settles into a stable idle: sampling the 68k PC finds it almost
entirely in the VIA dispatcher and the VBL stack sniffer, A7 holds steady
around `0x3F5FA8` so nothing is leaking exception frames, and only the 60 Hz
tick asserts an interrupt. The main thread never gets further — no boot icon
is drawn (the frame buffer centre stays pure dither), no SCSI or IDE probing
happens, and only two CLUT entries are ever loaded, which is why the screen
shows black rather than grey.

For comparison, `pm6400` on the same emulator reaches the boot-wait loop
within seconds and draws the grey desktop, the blinking disk icon and the
cursor, idling at 68k PCs in RAM rather than in ROM interrupt handlers. Making
the 5200/6200 get that far is the next job; disassembling the ROM around the
startup sequence with Ghidra would probably beat poking at it one routine at
a time.

Everything below is still a considered guess:

* **The device at `0x50F0E000`.** Written from `0x403030E8`-`0x40303464`
  through an index register at `+0x7C` with data at `+0x04`..`+0x34`, and read
  back at `+0x2C`. Also probed at `0x50F0A000` and `0x50F1FC00` later in the
  boot. Unidentified; the writes are currently logged and dropped.
* **Where Valkyrie's VBL lands.** MkLinux registers its VBL handler on F108 flag
  bit 6, so that is what is wired up, but the VIA2 slot register also has a
  "video" bit.
* **VIA2 flag bit 5.** Used here for the floppy controller, as AMIC does.
  MkLinux labels it "any button".
* **Sound.** Nothing is played. The FIFO status register reports both FIFOs as
  having room so the chime routine at `0x403045D0` runs to completion instead
  of spinning; samples are discarded.

Note also that MkLinux's own Performa port has rough edges that will show up
during bring-up: `grcswimiiihal.c` routes floppy access through AMIC DMA calls
on a machine with no AMIC and no DMA engine, and `PERFORMA_CUDA_BASE_PHYS` is
`0x50F16000`, which is the *floppy* base on the PDM machines.
