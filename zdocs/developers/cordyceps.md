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

See [cordyceps-handoff.md](cordyceps-handoff.md) for what currently runs, how
to launch it and which approaches are already known not to work.

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
drive 68k-style autovector levels. Capella requests a 603 interrupt whenever
the resulting priority level *changes away from the one the nanokernel last
acknowledged* - including when it falls back to idle. (MkLinux acknowledges at
`+0x18` rather than `+0x1C`; both addresses are accepted.)

Getting that edge condition right matters, because the nanokernel's external
interrupt handler is the only thing that ever writes the emulated 68k's
interrupt priority level:

```
403158a8  lwz  r0,0x1C(r2)     ; r2 = 0x53000000, acknowledge...
403158b4  stw  r0,0x1C(r2)     ; ...by writing zero back
403158c8  lwz  r2,0x24(r2)     ; read the level Capella now reports
403158d0  xori r2,r2,7         ; active low, so invert it
40315904  rlwinm. r2,r2,0,29,31
40315908  sth  r2,0(r3)        ; store it as the 68k IPL
40315918  beq  +0x20           ; level 0: nothing to post
```

The explicit `level == 0` case is the tell: Capella has to interrupt on the
falling edge too, or nothing would ever lower the stored IPL. Assert only on a
rising edge and the 68k believes an interrupt is pending forever - its level-1
autovector handler at `0x1C2F0` reads `IFR & IER`, gets zero, dispatches to
`Lvl1DT[7]` (an `rts`), `rte`s, and is re-entered the instant the restored
status register unmasks. The give-away in a trace is an endless alternation of
VIA1 register D and register E reads and nothing else.

```
IDE0/IDE1 -> F108 IFR -> VIA2 slot IFR bit 0 -> VIA2 IFR bit 1 -> level 2
Valkyrie VBL ----------> VIA2 slot IFR bit 6 -> VIA2 IFR bit 1 -> level 2
SCSI, sound, floppy -------------------------> VIA2 IFR --------> level 2
Cuda, 60.15 Hz tick, timers -----------------> VIA1 ------------> level 1
SCC ----------------------------------------------------------> level 4
```

The VIA2 slot flags use reverse logic (0 means asserted). An F108 flag is
cleared either by writing a 1 to the flag register at `0x1A101` or by writing
the bit to `0x1A100`, which is what MkLinux's handler does; until it is
cleared the F108 holds its output asserted. Nothing masks the slot lines
individually - VIA2's IER is what decides whether any of this reaches the 68k.

Putting the vertical blank on the F108 instead, where MkLinux registers its own
VBL handler, stops Mac OS dead with `unserviceable slot interrupt`.

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

## Status

Both machines boot the ROM all the way to the Mac OS "insert disk" screen: the
grey desktop dither, a live mouse cursor driven by Cuda ADB autopolling, and
the blinking floppy-with-question-mark. With nothing attached the 68k idles at
ROM `0x3F230` with `SR = 0x2000`, which is the normal boot-wait loop.

Two bugs stood between POST and that screen, and both are worth remembering
because neither announces itself:

* **The interrupt latch.** See the Interrupts section above. Asserting the 603
  request only on a rising edge wedges the machine inside the first VBL, and
  the symptom - a 68k handler re-entering itself forever - looks nothing like
  an interrupt-delivery problem from the outside.
* **Valkyrie register 5.** The ROM's "wait for vertical blanking" routine, in
  RAM at `0x22942`, clears the latched VBL interrupt by writing 1 to register 4
  and then spins on bit 0 of *register 5* until the next blanking interval sets
  it again:

  ```
  2295E  movea.l $18(a3),a0     ; Valkyrie base from the driver globals
  22962  move.b  #$1,$10(a0)    ; clear the latch (reg 4)
  2296A  lea.l   $14(a0),a0
  2296E  move.b  (a0),d0        ; read reg 5
  22972  btst.b  #$0,d0
  22976  beq.b   $2296e
  ```

  Register 5 is the readable side of the interrupt status; register 4 is the
  write-to-clear side. Only implementing the read on register 4 leaves the ROM
  spinning here forever.

## Booting from disk

With an image on the IDE bus the ROM boots Mac OS off it and hands over to
whatever is on the volume. Three things in the shared ATA and SCSI code had to
be fixed to get there, none of them specific to this machine:

* **32-bit PIO on the data port.** The ROM's ATA driver reads the data register
  with `move.l (a0),(a1)+` unrolled eight times, and a 32-bit access to a
  16-bit port moves two words - the first of them at the lower address, so the
  high half for a big endian guest. `IdeChannel::read` ignored the access size
  and returned a single word, which handed the driver every other word of the
  IDENTIFY response padded with zeroes.
* **READ BUFFER and WRITE BUFFER.** Apple's ATA manager writes a pattern into
  the drive's sector buffer and reads it back to work out how wide a transfer
  the interface will take. A drive that rejects the pair is not used at all:
  ours set ERR, and the driver polled the status register forever.
* **A null DMA channel.** These machines have no DMA engine, so nothing ever
  calls `Sc53C94::connect` and `channel_obj` stays null. `sequencer()`
  dereferenced it in `XFER_BEGIN` as soon as a target was selected.

What a successful boot looks like: IDENTIFY, the buffer test, INITIALIZE DEVICE
PARAMETERS, then reads of sectors 1 to 4 - the Apple partition map, whose first
bytes come back as `45 52 02 00`, the `ER` signature and a 512-byte block size -
and from there several hundred reads across the volume.

Two things to know before trying it:

* **8 MB of RAM is not enough.** The boot runs off the end of memory, logging
  accesses to `0x00800000`, and dies. Use `--rambank1_size 32`.
* **Use `--hdd2_img` for a second disk.** It lands on `Ide0:1`, the slave of the
  first channel, which is where a machine with one IDE connector has to put it.
  `hdd2_config` moves it elsewhere.

`hdd_img` used to be attached to the SCSI bus as well as the IDE one, because
the property is global and both `AtaHardDisk` and `ScsiBus` read it, so the ROM
found a second copy of the disk at SCSI ID 0. `ScsiBus` now leaves `hdd_img`
alone on any machine that has an IDE hard disk.

## SCSI

The processor moves SCSI data itself through the handshake port at
`0x50F10100`, and the ROM's driver will not touch that port until the 53C94
reports terminal count:

```
@loop:
  btst #4,(0x40,a3)    ; Status bit 4, terminal count
  bne.s @drain
  bsr.w <poll for the interrupt bit>
  and.b (0x40,a3),d5
  cmpi.b #1,d5         ; still DATA_IN, keep waiting
  beq.s @loop
@drain:
  btst #4,(0x70,a3)    ; FIFO Flags bit 4, sixteen bytes available
  beq.s @loop
  move.w (a1),(a2)+    ; read the handshake port
```

The transfer counter follows the SCSI bus, not the port the processor collects
from, so it reaches zero when the bus side finishes - which for a transfer that
fits in the sixteen byte FIFO is as soon as the FIFO is full. Setting the bit
only in response to a read through the handshake port, as the DMA path does,
deadlocks: the driver waits for terminal count and terminal count waits for the
driver. `rcv_data()` now sets it when the bus side completes, for machines with
no DMA channel only.

Two more things were missing from that port. The driver switches from `move.w`
to `move.l` once it has the buffer aligned, so the port has to move two words
for a long access - the first one at the lower address, in the high half. Until
it did, every transfer stopped exactly halfway and Mac OS spun forever in
`SyncWait`, which is the ROM's SCSI Manager waiting on a request that had gone
quiet. And DATA_OUT had no path at all: `XFER_BEGIN` did nothing without a DMA
channel and `SeqState::SEND_DATA` was an empty case, so bytes piled into the
sixteen byte FIFO until it overflowed. The sequencer now waits in `SEND_DATA`
and pushes each FIFO load to the target.

## MkLinux

Mac OS boots off the IDE disk, launches the MkLinux booter, and the booter hands
off without crashing - but only once its root device actually exists.

The root device has to be there or the booter will not start. It is `/dev/sdb2`,
the *second* SCSI disk, so the volume has to be at the second SCSI ID - MkLinux
names disks in discovery order, not by ID, so one disk on its own is `sda` no
matter which ID it sits at:

```
--hdd_img macos.img --scsi_hdd_img macos.img:mklinux.img --rambank1_size 32
```

### Use the right kernel

MkLinux gained real support for this family on 31 July 2000, and the R2 disc
ships the result in `MkLinux-install/Place in Extensions Folder/Performas Use
This!/`: a `Mach Kernel` built 5 August 2000, alongside a `README-PERFORMA` with
David Gatwood's announcement. Its caveats are worth reading first - no sound,
"SCSI is working, but rather slow... partially a lack of pseudo-DMA code", and
serial implemented but untested.

That kernel goes in the System Folder's Extensions folder in place of the stock
one. It is 1,612,796 bytes against the 1,350,052 of the generic R2 kernel, so it
cannot be dropped into a disk image by overwriting; copy it in the guest, or
reallocate the fork.

None of this is optional. The generic kernel, and anything rebuilt from the
retained 12/24/1999 osfmk snapshot, predates the whole Performa port.

### The branch to `0x40802xxx` (pre-2000 kernels only)

A kernel from before that work dies on a wild branch into 68k ROM space:

```
0022F480  mr    r31,r3
0022F484  lwz   r0,32(r31)     ; a function pointer from [r31+0x20]
0022F488  cmpwi r0,0
0022F48C  beq   +0x310         ; skip if null
0022F490  mtlr  r0
0022F494  blrl                 ; call it
```

`r3` arrives as zero, so the load comes from address `0x20` - 68k exception
vector 8, the privilege violation vector, still holding what Mac OS left there -
and the 603 branches to it and takes an ISI it cannot service. The null check one
instruction earlier guards the field, not the object.

This is in the Mach kernel, not the booter. `0x22F460` is `zalloc()`: its caller
reads a global that `zinit(0x68, ..., "vm objects")` fills in, which makes the
global `vm_object_zone` and the caller `vm_object_allocate()`. Walking the stack
back gives `kmem_alloc` → `kernel_memory_allocate` → `vm_object_allocate` →
`zalloc(NULL)`, and above that a video board probe whose panic string is
`valkyrie_probe: no memory available!`.

So: `go()` calls `initialize_screen()`, which probes the video boards, long
before `vm_mem_bootstrap()` has created `vm_object_zone`. `valkyrie_probe()`
calls `kmem_alloc()` from there, on the `POWERMAC_CLASS_PERFORMA` path only.
It cannot ever have worked. The node it allocates is not read back on that path
either - `valkyrie_init()` uses `PERFORMA_VIDEO_CLUT` directly for this class -
and the store through `valkyrie_node->addrs[0]` is a second null dereference,
because `kmem_alloc` returns zeroed memory and `addrs` is a pointer. Dropping
the whole allocation and returning TRUE is enough:
[`valkyrie-probe-performa.patch`](../../mklinux-selfhost/fixed-src/valkyrie-probe-performa.patch).

`PERFORMA_VIDEO_BASE` is `0x50F2A000` and `PERFORMA_VIDEO_CLUT` `0x50F24000`,
which is exactly where `ValkyrieVideo` puts its control and CLUT regions.

### Where it gets to

With that patched into the guest's kernel, the microkernel comes up on the
Valkyrie console:

```
Mach 3.0 VERSION(GENERIC_8.): root <osfmk>; ...
MACH microkernel is booting on a Power Macintosh Performa (unsupported) class
machine via Apple MkLinux Booter...
mem_size = 32 M
Mapping exception entry/exit 0x2000 to 0x2000 size 0x3000
kernel: mapping virt 0x00200000 to phys 0x00200000 size 0xca000, prot=0x5<READ,EXEC>
kernel: mapping virt 0x00400000 to phys 0x00400000 size 0x70000, prot=0x3<READ,WRITE>
bootstrap: mapping virt 0x00471000 to phys 0x00471000 end 0x474000, prot=0x3<READ,WRITE>
bootstrap: mapping virt 0x00473000 to phys 0x00473000 end 0x4a6000, prot=0x5<READ,EXEC>
WARNING - bootstrap overlaps regions
Free region start 0x00003000 end 0x00200000
Free region start 0x002ca000 end 0x00400000
Free region start 0x004a9000 end 0x004c0000
Free region start 0x005b8000 end 0x02000000
vm_page_bootstrap: 7418 free pages
```

and stops there, in a fault loop between the DSI vector at `0x300` and the
exception entry code at `0x2000`.

Do not reach for `mach_options=-r` to get a serial console out of a kernel of
that vintage. The option does arrive - the booter's dialog prints
`Mach Options: -r` and `parse_args()` acts on it - but it switches
`cons_ops_index` to the SCC before `initialize_serial()` has filled in
`scc_softc`, so the first `printf` lands in `scc_putc()`, which spins forever
waiting for `SCC_RR0_TX_EMPTY` from a channel whose register pointer is still
null. The video console is the one that works.

### Where the Performa kernel gets to

All the way. Mach comes up, hands over, and MkLinux DR3 reaches a login prompt
in about two minutes:

```
Mach 3.0 VERSION(GENERIC_8.): root <osfmk>; Sat Aug  5 17:23:47 PDT 2000; ...
Emulating 32 MB of physical memory from 0x142c0000 to 0x162c0000
using video mode 3 (640x480 at 50Hz interlaced), 8 bits/pixel
Console: colour osfmach3_vc 80x30, 1 virtual console (max 63)
Calibrating delay loop.. ok - 80.49 BogoMIPS
Linux version 2.0.33-osfmach3 (gilbert@venus.apple.com) ...
Mach root device sd1b: major=0 minor=18
VFS: Mounted root (ext2 filesystem) readonly.
INIT: version 2.74 booting
...
MkLinux for Power Macintosh. Brought to you by Apple Computer, Inc.
Developer Release 3 (Linux 2.0.33-osfmach3 on a PowerPC 603)
Based on Red Hat Linux release 5.0 (Hurricane)

mklinux login:
```

Getting the drive interrupt to Mach took most of the work. It arrives as
`IntSrc::IDE0` → the F108's flag register → the F108's output → the VIA2 slot
register → VIA2's IFR bit 1 → the 68k level-2 interrupt, and every stage was
wrong in some way:

* **VIA2 IFR writes only cleared a flag when bit 7 was set.** That is the IER's
  set/clear convention. On a 6522 a write of a one to an IFR bit clears it and
  bit 7 is the read-only summary, so MkLinux's `*PERFORMA_VIA2_IFR = 0x02`, and
  the `0x7F` its interrupt setup writes, both did nothing.
* **The slot cascade was gated on a per-slot enable register.** The developer
  note does describe one - "generates a level-2 interrupt if the slot interrupt
  enable bit is set" - but nothing in MkLinux writes it, and the mask that
  actually matters here is VIA2's own IER.
* **Valkyrie's vertical blank was on the F108's "Keystone" flag.** With the
  cascade live that reaches Mac OS as slot 0, which stops with `Sorry, a system
  error occurred: unserviceable slot interrupt` - Mac OS's own diagnosis, and
  the fastest way to tell the two apart. It belongs on the slot register's video
  line, leaving the F108 output to the drives.
* **Nothing dismissed an F108 flag.** MkLinux's handler writes the source's bit
  to `0x50F1A100`, the register below the flags, and then zero. While a flag is
  set the output stays down and no second interrupt arrives.

`performa_via2_slot_interrupt` is worth reading before touching any of this. It
dismisses its own interrupt with a bare `0x02` to the VIA2 IFR and then calls
the F108 handler *unconditionally*, without consulting the slot register - so
VIA2 IFR bit 1 is the only thing that has to arrive.

### What is still wrong

* **SCSI is slow, and that part is faithful.** This machine manages around
  150 KB/s where a 6100 over AMIC DMA manages 6.9 MB/s: MkLinux moves every
  byte through the FIFO register, with roughly three chip commands each, having
  no pseudo-DMA path at all. That is exactly the `SCSI is working, but rather
  slow... partially a lack of pseudo-DMA code` its README warns about. A clean
  boot takes about two minutes.

  Making the chip fill its whole FIFO on a non-DMA `TRANSFER` command instead of
  handing over one byte does triple the throughput - and breaks the 6100, whose
  ROM then never gets through its own disk scan. Mac OS's SCSI Manager depends
  on one byte per non-DMA transfer, so that is the real behaviour of the part
  and the slowness belongs to the guest.

* **MkLinux's ATAPI probe talks to addresses the F108 does not decode.** Its
  ATAPI accessors choose four byte register spacing only for
  `POWERMAC_CLASS_POWERBOOK`, so on a Performa they write Device/Head to
  `0x50F1A060` and commands to `0x50F1A070`, sixteen bytes apart, while the
  `wdc` driver beside them tests for PERFORMA too and gets it right:

  ```
  002D7568  lwz   r0,-0x30A8(r9)   ; powermac_info.class
  002D756C  xori  r11,r0,4         ; zero iff POWERBOOK
     ...                           ; r11 = 0 for POWERBOOK, -1 otherwise
  002D7580  addi  r9,r31,0x60      ; sixteen byte spacing
  002D7584  addi  r0,r31,0x18      ; four byte spacing
  002D7588  and   r9,r9,r11
  002D758C  andc  r0,r0,r11
  002D7590  or    r11,r9,r0        ; pick one
  ```

  **Leave it alone.** Those writes go nowhere on a real 6200 either, the CD-ROM
  on these machines is on the SCSI bus, and this is the kernel its author booted
  on a 6214. It only looked fatal because two bugs of ours turned a probe of the
  absent device 1 into a wedge: an absent device that read back as ready with
  DRQ asserted (`35f26ca0`), and a channel that stayed pointed at it across the
  software reset the driver used to recover (`6b9709dd`). With those fixed the
  probe is just noise in a trace.

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
