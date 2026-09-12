// Mach64 GX register, framebuffer aperture, and 2D drawing regression tests.
#include <devices/video/atimach64gx.h>
#include <machines/machineproperties.h>
#include <loguru.hpp>

#include <cstdio>

struct GxProbe : AtiMach64Gx {
    using AtiMach64Gx::read_reg;
    using AtiMach64Gx::write_reg;
};

int main() {
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
    gMachineSettings["gfxmem_size"] = std::make_unique<IntProperty>(2);
    gMachineSettings["mon_id"] = std::make_unique<StrProperty>("VGA-SVGA");
    GxProbe card;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* name) {
        ++checks;
        if (!ok) { std::printf("FAIL %s\n", name); ++failures; }
    };
    uint32_t chip = card.read_reg(ATI_CONFIG_CHIP_ID * 4, 4);
    check((chip & 0xFFFF) == 0xD7, "GX chip type uses original ATI encoding");
    card.write_reg(ATI_CONFIG_CHIP_ID * 4, 0, 4);
    card.write_reg(ATI_CONFIG_CHIP_ID * 4 + 3, 0xFF, 1);
    check(card.read_reg(ATI_CONFIG_CHIP_ID * 4, 4) == chip,
          "chip identification ignores DWORD and byte writes");
    card.write_reg(ATI_DAC_REGS * 4, 7, 1);
    for (uint8_t component : {0x12, 0x34, 0x56})
        card.write_reg(ATI_DAC_REGS * 4 + 1, component, 1);
    card.write_reg(ATI_DAC_REGS * 4 + 3, 7, 1);
    for (uint8_t component : {0x12, 0x34, 0x56})
        check(card.read_reg(ATI_DAC_REGS * 4 + 1, 1) == component,
              "DAC byte reads preserve the selected register lane");
    check(card.read_reg(ATI_DAC_REGS * 4 + 2, 1) == 0xFF, "DAC pixel mask byte reads back");

    // Native VRAM bytes are little-endian; the upper aperture swaps within
    // each pixel, including subword accesses, according to MEM_PIX_WIDTH.
    struct Mode { unsigned width; uint32_t native; };
    for (const auto mode : {Mode{2, 0x11223344}, Mode{3, 0x22114433},
                            Mode{4, 0x22114433}, Mode{6, 0x44332211}}) {
        card.write_reg(ATI_MEM_CNTL * 4, mode.width << 24, 4);
        card.write(0, BE_FB_OFFSET, 0x11223344, 4);
        check(card.read(0, 0, 4) == mode.native, "upper aperture shares native VRAM with pixel swapping");
        check(card.read(0, BE_FB_OFFSET, 4) == 0x11223344, "upper aperture DWORD round trip");
        card.write(0, BE_FB_OFFSET + 1, 0xAA, 1);
        check(card.read(0, BE_FB_OFFSET, 4) == 0x11AA3344, "byte write preserves other pixel bytes");
        card.write(0, BE_FB_OFFSET + 2, 0xBBCC, 2);
        check(card.read(0, BE_FB_OFFSET, 4) == 0x11AABBCC, "word write preserves adjacent bytes");
    }
    card.write(0, BE_FB_OFFSET + (2 << 20) - 1, 0xFFFFFFFF, 4);
    check(card.read(0, BE_FB_OFFSET + (2 << 20) - 1, 4) == 0,
          "access crossing the end of VRAM is rejected");

    auto reg = [&](unsigned index, uint32_t value) { card.write_reg(index * 4, value, 4); };
    reg(ATI_DP_PIX_WIDTH, 0x202);
    reg(ATI_DP_SRC, 0x100);
    reg(ATI_DP_MIX, 0x70007);
    reg(ATI_DP_WRITE_MSK, 0xF0);
    reg(ATI_DP_FRGD_CLR, 0xAB);
    reg(ATI_DST_OFF_PITCH, 1 << 22); // eight pixels per row
    reg(ATI_DST_CNTL, 3);
    reg(ATI_DST_Y_X, (2 << 16) | 1);
    reg(ATI_SC_LEFT_RIGHT, 4 << 16);
    reg(ATI_SC_TOP_BOTTOM, 7 << 16);
    for (unsigned offset = 0; offset < 64; offset += 4)
        card.write(0, offset, 0x3C3C3C3C, 4);
    reg(ATI_DST_HEIGHT_WIDTH, (4 << 16) | 2);
    check(card.read(0, 8, 4) == 0x3C3CACAC && card.read(0, 12, 4) == 0xAC3C3C3C &&
          card.read(0, 16, 4) == 0x3C3CACAC && card.read(0, 20, 4) == 0xAC3C3C3C,
          "rectangle fill clips and preserves masked destination bits");
    check(card.read(0, 0, 4) == 0x3C3C3C3C && card.read(0, 24, 4) == 0x3C3C3C3C,
          "rectangle fill leaves neighboring rows intact");

    card.write(0, 32, 0x01020304, 4);
    reg(ATI_DP_WRITE_MSK, 0xFF);
    reg(ATI_DP_SRC, 0x300);
    reg(ATI_SRC_OFF_PITCH, 1 << 22);
    reg(ATI_SRC_Y_X, (3 << 16) | 4);
    reg(ATI_DST_Y_X, (4 << 16) | 4);
    reg(ATI_DST_CNTL, 2); // walk right to left for overlapping copy
    reg(ATI_DST_HEIGHT_WIDTH, (4 << 16) | 1);
    check(card.read(0, 33, 4) == 0x01020304, "backward blit preserves overlapping source pixels");

    reg(ATI_DP_SRC, 0x10100);
    reg(ATI_DP_FRGD_CLR, 0xFF);
    reg(ATI_DP_BKGD_CLR, 0);
    reg(ATI_PAT_REG0, 0xAA);
    reg(ATI_DST_Y_X, 0);
    reg(ATI_DST_CNTL, 3);
    reg(ATI_SC_LEFT_RIGHT, 7 << 16);
    reg(ATI_DST_HEIGHT_WIDTH, (8 << 16) | 1);
    check(card.read(0, 0, 4) == 0xFF00FF00 && card.read(0, 4, 4) == 0xFF00FF00,
          "monochrome pattern chooses foreground and background colors");
    reg(ATI_PAT_REG0, 0x55);
    reg(ATI_DP_PIX_WIDTH, 0x01000202);
    reg(ATI_DST_HEIGHT_WIDTH, (8 << 16) | 1);
    check(card.read(0, 0, 4) == 0xFF00FF00 && card.read(0, 4, 4) == 0xFF00FF00,
          "monochrome pattern honors LSB-first pixel order");
    reg(ATI_DP_SRC, 0x100);
    reg(ATI_DP_FRGD_CLR, 0xAA);
    reg(ATI_DP_MIX, 5 << 16);
    reg(ATI_DST_HEIGHT_WIDTH, (4 << 16) | 1);
    check(card.read(0, 0, 4) == 0x55AA55AA, "XOR raster operation combines source and destination");
    reg(ATI_DP_MIX, 7 << 16);
    reg(ATI_DP_PIX_WIDTH, 0x303);
    reg(ATI_DP_FRGD_CLR, 0x1234);
    reg(ATI_DP_WRITE_MSK, 0xFFFF);
    reg(ATI_MEM_CNTL, 3 << 24);
    reg(ATI_DST_HEIGHT_WIDTH, (1 << 16) | 1);
    check(card.read(0, 0, 2) == 0x3412 && card.read(0, BE_FB_OFFSET, 2) == 0x1234,
          "RGB555 engine writes native VRAM and agrees with big-endian aperture");
    for (unsigned offset = 0; offset < 32; offset += 4)
        card.write(0, offset, 0x77777777, 4);
    reg(ATI_DP_PIX_WIDTH, 0x202); // one-bit host source, eight-bit destination
    reg(ATI_DP_SRC, 0x20100);
    reg(ATI_DP_MIX, 0x70003); // transparent zero bits
    reg(ATI_DP_FRGD_CLR, 0xFF);
    reg(ATI_DP_WRITE_MSK, 0xFF);
    reg(ATI_HOST_CNTL, 1); // align each source row to a byte
    reg(ATI_DST_HEIGHT_WIDTH, (3 << 16) | 2);
    check(card.read(0, 0, 4) == 0x77777777, "host rectangle waits for bitmap data");
    reg(ATI_HOST_DATA0, 0x000040A0); // rows 101 and 010, MSB first within each byte
    check(card.read(0, 0, 4) == 0xFF77FF77 && card.read(0, 8, 4) == 0x77FF7777,
          "host monochrome expansion handles byte padding and transparent background");
    reg(ATI_HOST_DATA15, 0xFFFFFFFF);
    check(card.read(0, 16, 4) == 0x77777777, "extra host data does not draw past completed rectangle");
    reg(ATI_DP_PIX_WIDTH, 0x01000202); // LSB first within each byte
    reg(ATI_DST_Y_X, 2);
    reg(ATI_SC_LEFT_RIGHT, (7 << 16) | 1);
    reg(ATI_DST_HEIGHT_WIDTH, (3 << 16) | 1);
    card.write_reg(ATI_HOST_DATA0 * 4, 5, 1);
    check(card.read(0, 16, 4) == 0x7777FF77,
          "clipped host pixels still consume source bits and honor LSB-first order");
    reg(ATI_DST_OFF_PITCH, (2 << 22) | 8); // sixteen-pixel row at byte 64
    reg(ATI_DST_Y_X, 15 << 16);
    reg(ATI_DST_CNTL, 2);
    reg(ATI_SC_LEFT_RIGHT, 15 << 16);
    reg(ATI_DP_PIX_WIDTH, 0x202);
    reg(ATI_DP_MIX, 0x70007);
    reg(ATI_DST_HEIGHT_WIDTH, (16 << 16) | 1);
    reg(ATI_HOST_DATA0, 0x80010000);
    check(card.read(0, 64, 4) == 0xFF000000 && card.read(0, 76, 4) == 0x000000FF,
          "right-to-left host expansion consumes the most significant byte first");
    for (unsigned offset = 64; offset < 96; offset += 4)
        card.write(0, offset, 0x80808080, 4);
    reg(ATI_DP_PIX_WIDTH, 0x020202);
    reg(ATI_DP_SRC, 0x200); // color host pixels
    reg(ATI_DP_MIX, 11 << 16); // OR, as used by the -101 Mac OS driver
    reg(ATI_DST_CNTL, 3);
    reg(ATI_DST_Y_X, 0);
    reg(ATI_SC_LEFT_RIGHT, 2 << 16);
    reg(ATI_DST_HEIGHT_WIDTH, (3 << 16) | 2);
    reg(ATI_HOST_DATA0, 0x04030201);
    check(card.read(0, 64, 4) == 0x81828380 && card.read(0, 80, 4) == 0x84808080,
          "eight-bit host pixels apply OR and continue across rows within a DWORD");
    card.write_reg(ATI_HOST_DATA15 * 4, 0x0605, 2);
    check(card.read(0, 80, 4) == 0x84858680,
          "color host transfer resumes at the next pixel and stops at rectangle end");
    std::printf("Mach64 GX: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
