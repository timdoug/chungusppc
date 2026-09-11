The AMIC is the I/O controller used in the Power Mac 6100, 7100 and 8100. Its name abbreviates Apple Memory Mapped I/O Controller.
Physically, it's located at 0x50F00000.

It also:

* Controls the video timing signals

## Subdevices

| Subdevice      | Range                |
|:--------------:|:--------------------:|
| VIA 1          | 0x0 - 0x1FFF         |
| SCC            | 0x4000 - 0x5FFF      |
| MACE           | 0xA000 - 0xBFFF      |
| SCSI A         | 0x10000 - 0x10FFF    |
| SCSI B (8100)  | 0x11000 - 0x11FFF    |
| AWACS          | 0x14000 - 0x15FFF    |
| SWIM III       | 0x16000 - 0x17FFF    |
| VIA 2          | 0x26000 - 0x27FFF    |
| Video          | 0x28000 - 0x29FFF    |
| DMA            | 0x31000 - 0x32FFF    |
| Mem Control    | 0x40000 - 0x41FFF    |

The 8100's second SCSI controller has its own 53C94-compatible register bank,
FIFO, bus and `AmicScsiDma` instance. Its DMA base occupies `0x32004–0x32007`
and its control register is at `0x32009`; the first controller uses
`0x32000–0x32003` and `0x32008`. VIA2 IFR/IER bit 6 carries the second controller's
IRQ, and bit 2 carries its DRQ (the first controller uses bits 3 and 0).
These assignments are also documented by MkLinux's `scsi_amic.c`,
`interrupt_pdm.c` and `powermac_pdm.h` in the Mach source archive.

The second controller is absent on the 6100 and 7100. Reads of its register bank
return zero there, allowing the ROM's FIFO probe to detect its absence.
