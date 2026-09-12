// ATI Rage drawing paths used by the Mac OS 7.6.1 Gazelle driver.
#include <devices/video/atirage.h>
#include <machines/machineproperties.h>
#include <loguru.hpp>

#include <cstdio>

struct RageProbe : ATIRage {
    RageProbe() : ATIRage(ATI_RAGE_GT_DEV_ID) {}
    using ATIRage::read_reg;
    using ATIRage::write_reg;
};

int main() {
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
    gMachineSettings["gfxmem_size"] = std::make_unique<IntProperty>(2);
    gMachineSettings["mon_id"] = std::make_unique<StrProperty>("VGA-SVGA");
    RageProbe card;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* name) {
        ++checks;
        if (!ok) { std::printf("FAIL %s\n", name); ++failures; }
    };
    auto reg = [&](unsigned index, uint32_t value) { card.write_reg(index * 4, value, 4); };
    auto reset = [&] {
        for (unsigned offset = 0; offset < 256; offset += 4)
            card.write(0, offset, 0x77777777, 4);
        reg(ATI_DP_PIX_WIDTH, 0x202);
        reg(ATI_DP_SRC, 0x100);
        reg(ATI_DP_MIX, 0x70007);
        reg(ATI_DP_WRITE_MSK, 0xFFFFFFFF);
        reg(ATI_DP_FRGD_CLR, 0xFF);
        reg(ATI_DP_BKGD_CLR, 0);
        reg(ATI_CLR_CMP_CNTL, 0);
        reg(ATI_SRC_CNTL, 0);
        reg(ATI_HOST_CNTL, 1);
        reg(ATI_DST_OFF_PITCH, 1 << 22); // eight pixels per row
        reg(ATI_DST_CNTL, 3);
        reg(ATI_DST_Y_X, 0);
        reg(ATI_SC_LEFT_RIGHT, 7 << 16);
        reg(ATI_SC_TOP_BOTTOM, 7 << 16);
    };

    reset();
    reg(ATI_DP_SRC, 0x20100);
    reg(ATI_DP_MIX, 0x70003); // opaque foreground, transparent background
    reg(ATI_DST_HEIGHT_WIDTH, (3 << 16) | 2);
    check(card.read(0, 0, 4) == 0x77777777, "monochrome upload waits for host data");
    reg(ATI_HOST_DATA0, 0x000040A0); // padded rows 101 and 010
    check(card.read(0, 0, 4) == 0xFF77FF77 && card.read(0, 8, 4) == 0x77FF7777,
          "monochrome host data expands padded rows with transparent background");
    reg(ATI_HOST_DATA15, 0xFFFFFFFF);
    check(card.read(0, 16, 4) == 0x77777777, "host data after rectangle completion is ignored");

    reset();
    reg(ATI_DP_SRC, 0x20100);
    reg(ATI_DP_PIX_WIDTH, 0x01000202); // least significant bit first
    reg(ATI_SC_LEFT_RIGHT, (7 << 16) | 1);
    reg(ATI_DST_HEIGHT_WIDTH, (3 << 16) | 1);
    card.write_reg(ATI_HOST_DATA0 * 4, 5, 1);
    check(card.read(0, 0, 4) == 0x7700FF77,
          "clipped upload consumes source bits and honors LSB-first order");

    reset();
    reg(ATI_DP_SRC, 0x20100);
    reg(ATI_HOST_CNTL, 0); // packed rows: 101 010
    reg(ATI_DST_HEIGHT_WIDTH, (3 << 16) | 2);
    card.write_reg(ATI_HOST_DATA0 * 4, 0xA8, 1);
    check(card.read(0, 0, 4) == 0xFF00FF77 && card.read(0, 8, 4) == 0x00FF0077,
          "packed monochrome rows continue within a source byte");

    reset();
    reg(ATI_DP_SRC, 0x20100);
    reg(ATI_DST_OFF_PITCH, 2 << 22);
    reg(ATI_DST_Y_X, 15 << 16);
    reg(ATI_DST_CNTL, 2);
    reg(ATI_SC_LEFT_RIGHT, 15 << 16);
    reg(ATI_DST_HEIGHT_WIDTH, (16 << 16) | 1);
    reg(ATI_HOST_DATA0, 0x80010000);
    check(card.read(0, 0, 4) == 0xFF000000 && card.read(0, 12, 4) == 0x000000FF,
          "right-to-left upload consumes the most significant byte first");

    reset();
    reg(ATI_DP_SRC, 0x10100);
    reg(ATI_PAT_REG0, 0xAA);
    reg(ATI_PAT_REG1, 0x55);
    reg(ATI_DST_HEIGHT_WIDTH, (8 << 16) | 5);
    check(card.read(0, 0, 4) == 0xFF00FF00 && card.read(0, 32, 4) == 0x00FF00FF,
          "8x8 monochrome patterns use both pattern registers");
    reg(ATI_DP_PIX_WIDTH, 0x01000202);
    reg(ATI_DST_HEIGHT_WIDTH, (8 << 16) | 1);
    check(card.read(0, 0, 4) == 0x00FF00FF, "pattern honors LSB-first bit order");
    reg(ATI_DP_MIX, 0x30000); // Gazelle desktop pattern: keep/invert destination
    reg(ATI_DST_HEIGHT_WIDTH, (8 << 16) | 1);
    check(card.read(0, 0, 4) == 0xFFFFFFFF,
          "pattern selects foreground and background raster operations");

    reset();
    reg(ATI_DP_FRGD_CLR, 0xAB);
    reg(ATI_DP_WRITE_MSK, 0xF0);
    reg(ATI_DST_HEIGHT_WIDTH, (4 << 16) | 1);
    check(card.read(0, 0, 4) == 0xA7A7A7A7, "fill preserves masked destination bits");
    reg(ATI_DP_MIX, 0); // invert destination, used by the Mac OS driver
    reg(ATI_DST_HEIGHT_WIDTH, (4 << 16) | 1);
    check(card.read(0, 0, 4) == 0x57575757, "fill applies inversion through the write mask");

    for (auto format : {ATI_PIX_FMT_RGB555, ATI_PIX_FMT_ARGB8888}) {
        reset();
        reg(ATI_DP_PIX_WIDTH, format | (format << 8));
        reg(ATI_DP_SRC, 0x20100);
        reg(ATI_DP_FRGD_CLR, 0x12345678);
        reg(ATI_DST_HEIGHT_WIDTH, (1 << 16) | 1);
        card.write_reg(ATI_HOST_DATA0 * 4, 0x80, 1);
        check(card.read(0, BE_FB_OFFSET, format == ATI_PIX_FMT_RGB555 ? 2 : 4) ==
                  (format == ATI_PIX_FMT_RGB555 ? 0x5678U : 0x12345678U),
              "monochrome upload agrees with direct-color framebuffer byte order");
    }

    reset();
    reg(ATI_DP_PIX_WIDTH, 0x20202);
    reg(ATI_DP_SRC, 0x200);
    reg(ATI_DP_MIX, 11 << 16); // OR host color with existing framebuffer
    reg(ATI_DST_HEIGHT_WIDTH, (3 << 16) | 2);
    reg(ATI_HOST_DATA0, 0x80402010);
    card.write_reg(ATI_HOST_DATA15 * 4, 0x0201, 2);
    check(card.read(0, 0, 4) == 0x77777777 && card.read(0, 8, 4) == 0xF7777777,
          "color host upload applies OR and resumes across register writes and rows");

    reset();
    card.write(0, 0, 0x01234567, 4);
    reg(ATI_DST_Y_X, 0x3FFF << 16); // x = -1
    reg(ATI_DP_FRGD_CLR, 0xAB);
    reg(ATI_DST_HEIGHT_WIDTH, (3 << 16) | 1);
    check(card.read(0, 0, 4) == 0xABAB4567,
          "fill clips a negative start coordinate without shifting its right edge");

    reset();
    reg(ATI_DP_PIX_WIDTH, 0x404);
    reg(ATI_DP_SRC, 0x20100);
    reg(ATI_DP_FRGD_CLR, 0x1234);
    reg(ATI_DST_HEIGHT_WIDTH, (1 << 16) | 1);
    card.write_reg(ATI_HOST_DATA0 * 4, 0x80, 1);
    check(card.read(0, 0, 2) == 0x3412,
          "RGB565 upload retains the scanout's little-endian pixel order");

    reset();
    reg(ATI_DP_SRC, 0x20100);
    reg(ATI_DST_CNTL, 3 | (1 << ATI_DST_Y_TILE));
    reg(ATI_DST_HEIGHT_WIDTH, (2 << 16) | 2);
    card.write_reg(ATI_HOST_DATA0 * 4, 0x80, 1);
    check(card.read_reg(ATI_DST_Y * 4, 4) == 0,
          "host rectangle does not advance destination trajectory before completion");
    card.write_reg(ATI_HOST_DATA15 * 4, 0x40, 1);
    check(card.read_reg(ATI_DST_Y * 4, 4) == 2,
          "completed host rectangle advances destination trajectory once");

    std::printf("ATI Rage: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
