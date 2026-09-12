// Codec byte order, DMA underruns and descriptor-boundary sample conversion.
#include <core/endianswap.h>
#include <devices/sound/awacs.h>
#include <devices/sound/soundbuffer.h>
#include <machines/machinebase.h>
#include <loguru.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

struct AudioDma : DmaOutChannel {
    AudioDma() : DmaOutChannel("test audio") {}
    std::vector<uint8_t> bytes;
    size_t position = 0;
    uint32_t fragment = 1024;
    bool active = true;
    bool is_out_active() override { return active; }
    DmaPullResult pull_data(uint32_t requested, uint32_t* count, uint8_t** data) override {
        *count = std::min({requested, fragment, uint32_t(bytes.size() - position)});
        *data = bytes.data() + position;
        position += *count;
        return *count ? MoreData : NoMoreData;
    }
};

int main() {
    loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* name) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
    };
    const int16_t samples[] = {0x1234, -0x1234, 32767, -32768, 1, -1};
    for (bool swapped : {false, true}) {
        for (unsigned fragment : {1, 3, 4, 7, 64}) {
            AudioDma dma;
            dma.fragment = fragment;
            for (auto sample : samples) {
                dma.bytes.push_back(swapped ? sample & 255 : uint16_t(sample) >> 8);
                dma.bytes.push_back(swapped ? uint16_t(sample) >> 8 : sample & 255);
            }
            DmaSoundOutput output(&dma);
            output.set_byte_swap(swapped);
            int16_t result[10];
            output.render(result, 5);
            check(!std::memcmp(result, samples, sizeof(samples)),
                  "signed stereo samples survive byte order and descriptor boundaries");
            check(std::all_of(result + 6, result + 10, [](int16_t n) { return n == 0; }),
                  "short DMA fills the rest of the host buffer with silence");
            dma.active = false;
            output.render(result, 5);
            check(std::all_of(result, result + 10, [](int16_t n) { return n == 0; }),
                  "stopped guest DMA produces a full silent host buffer");
            dma.position = 0;
            dma.active = true;
            output.render(result, 3);
            check(!std::memcmp(result, samples, sizeof(samples)),
                  "playback resumes after an underrun without reopening the host stream");
        }
    }
    {
        AudioDma dma;
        dma.bytes = {0x12, 0x34, 0xED};
        DmaSoundOutput output(&dma);
        int16_t result[2];
        output.render(result, 1);
        check(result[0] == 0 && result[1] == 0, "incomplete stereo frame remains pending");
        dma.bytes.push_back(0xCC);
        output.render(result, 1);
        check(result[0] == 0x1234 && result[1] == -0x1234,
              "next DMA buffer completes the pending stereo frame");
    }
    gMachineObj = std::make_unique<MachineBase>("audio regression test");
    {
        AwacsScreamer codec;
        for (uint32_t swap : {0, 1, 0}) {
            codec.snd_ctrl_write(AWAC_BYTE_SWAP, BYTESWAP_32(swap), 4);
            check(codec.snd_ctrl_read(AWAC_BYTE_SWAP, 4) == BYTESWAP_32(swap),
                  "AWACS byte-swap register reads back the programmed little-endian value");
        }
    }
    gMachineObj.reset();
    std::printf("Audio tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
