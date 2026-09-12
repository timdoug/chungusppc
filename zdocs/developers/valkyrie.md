The Valkyrie video chip is included in some Quadras and Performas. The Marathon games (2 and Infinity, at least) use this chip to blit 16-bit video.

In 16-bit mode, each five-bit RGB field indexes its corresponding CLUT channel.
Mac OS supplies gamma ramps, while MkLinux uses the same console palette index
in all three fields. Rendering RGB555 values directly therefore produces a
grey console. Valkyrie uses `convert_frame_directcolor<16>` to apply the CLUT.
The Linux [Valkyrie driver](https://github.com/torvalds/linux/blob/master/drivers/video/fbdev/valkyriefb.c)
also demonstrates this mapping in `valkyriefb_setcolreg`.

| Register Name             | Offset |
|:-------------------------:|:------:|
| CLUT Address              | 0x4000 |
| CLUT Graphic              | 0x4004 |
| CLUT Video Data           | 0x4008 |
| CLUT Color Key            | 0x400C |
| Subsystem Config          | 0xA00C |
| Video In Control          | 0xA020 |
| Window X Start            | 0xA060 |
| Window Y Start            | 0xA064 |
| Window Width              | 0xA070 |
| Window Height             | 0xA074 |
| Video Field X Start       | 0xA080 |
| Video Field Y Start       | 0xA084 |
