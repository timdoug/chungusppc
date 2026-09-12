# ATI Mach64 GX

`AtiMach64Gx` models the PCI graphics card used for the 9500/9600 tests, with
an IBM RGB514 DAC and a separate expansion ROM. It is distinct from the
integrated ATI Rage devices. The card ROM is loaded from
`113-32900-004_Apple_MACH64.bin` in the emulator's working directory.

## Identification and apertures

`CONFIG_CHIP_ID` must report the GX chip type `0x00D7`, using ATI's original
five-bit character encoding. The PCI device ID is `0x4758`. Returning zero
makes the Macintosh driver choose the wrong framebuffer aperture; MkLinux
then derives an unmapped accelerator-register address from that framebuffer.
The identification register ignores writes.

The Macintosh card's 16 MiB BAR contains native little-endian VRAM at offset
zero, accelerator registers at `0x7FFC00`, and the big-endian VRAM aperture at
`0x800000`. Both framebuffer apertures access the same storage. `MEM_PIX_WIDTH`
in `MEM_CNTL[26:24]` selects swapping within 16-bit or 32-bit pixels in the
upper aperture. Byte and word accesses must use the same address permutation
as DWORD accesses. Scanout and accelerator writes use native little-endian
pixel storage.

The DAC's four byte registers share one accelerator DWORD. Reads must put the
DAC result in the requested byte lane before extracting that lane. Returning
it in lane zero made palette data and mask reads through the other lanes zero.

## Drawing

Mac OS 7.6.1 uses the accelerator for fills, screen copies, patterns, and
monochrome host bitmaps. Without those operations, windows overlap without
repainting and text or icons disappear after a color-depth change.

The implemented rectangle path includes solid colors, framebuffer sources,
8×8 monochrome patterns, source repetition, the sixteen Boolean raster
operations, write masks, clipping, and both drawing directions. It supports
8-bit, RGB555, RGB565 and 32-bit destination pixels. Combined coordinate and
trajectory registers update their individual aliases before a draw begins.

Monochrome host expansion waits for `HOST_DATA0..15` writes. It consumes
clipped source pixels, honors byte-aligned rows and pixel bit order, and stops
at the requested rectangle size. Host data bytes are consumed low to high
when drawing left to right and high to low when drawing right to left. Fixed
monochrome patterns also honor `DP_BYTE_PIX_ORDER` within each pattern byte.
Eight-bit color host transfers use the same clipping, masks and raster
operations. The `-101` ROM uses OR when uploading Finder bitmaps; accepting
only a straight source copy leaves labels missing after changing to 256 colors.

This is a synchronous subset of the engine. Lines, direct-color host transfers,
monochrome framebuffer sources, color comparison, and timed FIFO execution
remain unimplemented. RGB514 direct-color gamma/bypass behavior has not been
validated. Mac OS smoke tests cover 640×480 in 256, Thousands, and Millions
of colors, text/icons, changing depth and moving/repainting windows.

The `mach64gx` CTest checks identification, DAC byte reads, shared apertures,
subword swapping and bounds, clipped/masked fills, overlapping copies,
monochrome patterns, XOR, native RGB555 drawing, host monochrome expansion,
and eight-bit color host transfers with OR across rows and register writes.

## References

* [ATI Mach64 Programmer's Guide](https://bitsavers.trailing-edge.com/components/ati/PRG888GX0-01_ATI_Mach64_Accelerator_Programmers_Guide_Technical_Reference_Manual_1994.pdf),
  logical pixel data path and pattern consumption, especially page 2-36.
* [ATI Mach64 Register Reference](https://bitsavers.trailing-edge.com/components/ati/RRG-S00700-05_mach64_Register_Reference_Guide_1999410.pdf),
  `CONFIG_CHIP_ID`, `MEM_CNTL`, `DP_MIX`, `DP_PIX_WIDTH`, `HOST_CNTL`,
  `HOST_DATA0..15`, and the source/destination trajectory registers.
