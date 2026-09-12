// Convert the codec's stereo DMA bytes to host-native signed 16-bit samples.
#ifndef SOUND_BUFFER_H
#define SOUND_BUFFER_H

#include <core/endianswap.h>
#include <core/memaccess.h>
#include <devices/common/dmacore.h>

#include <algorithm>
#include <atomic>
#include <cstdint>

class DmaSoundOutput {
public:
    explicit DmaSoundOutput(DmaOutChannel* channel) : channel(channel) {}

    void set_byte_swap(bool enabled) { byte_swap = enabled; }

    void render(int16_t* output, long frames) {
        // An idle DMA channel is an underrun, not the end of the host stream.
        // Mach stops its descriptor list between writes and wakes it later.
        std::fill_n(output, frames * 2, int16_t(0));
        const bool little_endian = byte_swap.load();
        while (frames > 0 && channel->is_out_active()) {
            uint8_t* data = nullptr;
            uint32_t count = 0;
            if (channel->pull_data(frames * 4 - pending_size, &count, &data) !=
                    DmaPullResult::MoreData || !data || !count)
                break;
            // A descriptor can end in the middle of a stereo frame.
            for (uint32_t i = 0; i < count; ++i) {
                pending[pending_size++] = data[i];
                if (pending_size == 4) {
                    output[0] = little_endian ? READ_WORD_LE_U(pending) : READ_WORD_BE_U(pending);
                    output[1] = little_endian ? READ_WORD_LE_U(pending + 2) : READ_WORD_BE_U(pending + 2);
                    output += 2;
                    --frames;
                    pending_size = 0;
                }
            }
        }
    }

private:
    DmaOutChannel* channel;
    std::atomic<bool> byte_swap = false;
    uint8_t pending[4] = {};
    unsigned pending_size = 0;
};

#endif
