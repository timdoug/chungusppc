The ATI Rage is a video card that comes bundled with early Power Mac G3s and New World Macs (like the first revisions of the iMac G3). Its predecessor was the ATI Mach 64 GX, used in earlier Old World Macs.

For 2D acceleration, it can support window drawing, scrolling, and blitting. The card itself supports 8-bit color at minimum, with 32-bit color at maximum.

Officially, it only supports up to 6 MB, but hacks can be done such that it uses up to 8 MB.

Later Power Mac G3s would be bundled with the ATI Rage 128, which could support up to 32 MB of video memory.

RGB555 and xRGB8888 scanout use the DAC lookup table separately for red, green,
and blue. On integrated Mach64 chips, the five-bit RGB555 components address
entries `component << 3`; xRGB8888 uses the component byte directly. Mac OS
loads gamma ramps and MkLinux loads console colors. Treating the pixel fields
as linear intensities makes MkLinux's 16-bit console gray. This matches the
[Linux atyfb driver's palette programming](https://github.com/torvalds/linux/blob/master/drivers/video/fbdev/aty/atyfb_base.c)
and the retained Mach source's `video_ati.c:ati_setcolor`.

The `directcolor` CTest checks independent component lookup, the shifted RGB555
indices, palette changes, and framebuffer row strides. Mac OS 7.6.1 still uses
monochrome source and host drawing operations that this accelerator does not
implement; affected text and icons can be incomplete.

# Memory Map

The ATI Rage can usually be located at IOBase (ex.: 0xF3000000 for Power Mac G3 Beige) + 0x9000. However, the video memory appears to be at 0x81000000 and is capped at 8 MB.

# Register Map

| Register Name       | Offset |
|:-------------------:|:------:|
| BUS_CNTL            | 0xA0   |
| EXT_MEM_CNTL        | 0xAC   |
| MEM_CNTL            | 0xB0   |
| MEM_VGA_WP_SEL      | 0xB4   |
| MEM_VGA_RP_SEL      | 0xB8   |
| GEN_TEST_CNTL       | 0xD0   |
| CONFIG_CNTL         | 0xDC   |
| CONFIG_CHIP_ID      | 0xE0   |
| CONFIG_STAT0        | 0xE4   |
| DST_CNTL            | 0x130  |
| SRC_OFF_PITCH       | 0x180  |
| SRC_X               | 0x184  |
| SRC_Y               | 0x188  |
| SRC_Y_X             | 0x18C  |
| SRC_WIDTH1          | 0x190  |
| SRC_HEIGHT1         | 0x194  |
| SRC_HEIGHT1_WIDTH1  | 0x198  |
| SRC_CNTL            | 0x1B4  |
| SCALE_3D_CNTL       | 0x1FC  |
| HOST_DATA0          | 0x200  |
| HOST_CNTL           | 0x240  |
| DP_PIX_WIDTH        | 0x2D0  |
| DP_SRC              | 0x2D8  |
