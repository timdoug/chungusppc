// Framebuffer conversion tests; the display backend is replaced by a stub.
#include <core/memaccess.h>
#include <devices/video/videoctrl.h>
#include <loguru.hpp>

#include <array>
#include <cstdio>

struct VideoProbe : VideoCtrlBase {
    VideoProbe() : VideoCtrlBase(2, 2) {}
    void buffer(uint8_t* bytes, int pitch) { fb_ptr = bytes; fb_pitch = pitch; }
};

int main() {
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
    VideoProbe video;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* name) {
        ++checks;
        if (!ok) {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    };
    // Each component selects its own DAC entry. Apple uses low indices;
    // the integrated Mach64 uses the high five address bits in RGB555.
    for (int shift : {0, 3}) {
        std::array<uint8_t, 16> source{};
        std::array<uint32_t, 6> output;
        output.fill(0xDEADBEEF);
        video.buffer(source.data(), 8);
        video.set_palette_color(1 << shift, 0xA1, 0x12, 0x13, 255);
        video.set_palette_color(2 << shift, 0x21, 0xB2, 0x23, 255);
        video.set_palette_color(3 << shift, 0x31, 0x32, 0xC3, 255);
        WRITE_WORD_BE_A(source.data(), (1 << 10) | (2 << 5) | 3);
        WRITE_WORD_BE_A(source.data() + 8, (3 << 10) | (1 << 5) | 2);
        video.convert_frame_directcolor<16>(reinterpret_cast<uint8_t*>(output.data()), 12, shift);
        check(output[0] == 0xFFA1B2C3 && output[3] == 0xFF311223,
              "RGB555 uses separate DAC entries and respects source stride");
        check(output[2] == 0xDEADBEEF && output[5] == 0xDEADBEEF,
              "conversion preserves destination row padding");
        video.set_palette_color(1 << shift, 0x55, 0x12, 0x13, 255);
        video.convert_frame_directcolor<16>(reinterpret_cast<uint8_t*>(output.data()), 12, shift);
        check(output[0] == 0xFF55B2C3, "palette updates change existing pixels");
    }
    std::array<uint8_t, 24> source{};
    std::array<uint32_t, 6> output{};
    video.buffer(source.data(), 12);
    video.set_palette_color(0x81, 0xA1, 0, 0, 255);
    video.set_palette_color(0x92, 0, 0xB2, 0, 255);
    video.set_palette_color(0xA3, 0, 0, 0xC3, 255);
    WRITE_DWORD_BE_A(source.data(), 0xFF8192A3);
    WRITE_DWORD_BE_A(source.data() + 12, 0x008192A3);
    video.convert_frame_directcolor<32>(reinterpret_cast<uint8_t*>(output.data()), 12);
    check(output[0] == 0xFFA1B2C3 && output[3] == output[0],
          "xRGB8888 uses full DAC indices and ignores the unused byte");
    std::printf("Direct color: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
