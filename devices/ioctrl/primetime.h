/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team
          (See CREDITS.MD for more details)

(You may also contact divingkxt or powermax2286 on Discord)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/** @file PrimeTime II I/O controller definitions.

    PrimeTime II bridges the 68040 bus to the 16 MHz, 68030-style I/O bus of
    the Power Macintosh 5200/6200 family. It contains VIA1, VIA2, the SWIM II
    floppy controller, the interrupt logic and the sound buffers.

    The I/O page at 0x50F00000 is decoded by PrimeTime II, but several of the
    devices living in it (SCSI, SCC and IDE) are physically part of the F108.
    They are dispatched from here because the decode is a single switch.

    Register locations come from the block diagram and chip descriptions in the
    "Power Macintosh 5200/75 LC and 6200/75 Computers" developer note (pp. 14-22)
    and from MkLinux's ppc/POWERMAC/powermac_performa.h, which is the only
    register-level description of this machine we have. Interrupt bit
    assignments come from the tables in MkLinux's interrupt_performa.c.
 */

#ifndef PRIME_TIME_TWO_H
#define PRIME_TIME_TWO_H

#include <devices/common/hwcomponent.h>
#include <devices/common/hwinterrupt.h>
#include <devices/common/mmiodevice.h>

#include <cinttypes>
#include <memory>
#include <unordered_map>

class EsccController;
class IdeChannel;
class Sc53C94;
class ViaCuda;
namespace Swim3 { class Swim3Ctrl; }

namespace PrimeTime {
    constexpr uint32_t IO_BASE = 0x50F00000;
    // Valkyrie claims its own regions from 0x50F20000 up, so stop short of it.
    constexpr uint32_t IO_SIZE = 0x00020000;
    // The same devices answer at the raw decode too: the ROM's serial output
    // routine at 0x403079C4 reaches the SCC through 0x5000C000.
    constexpr uint32_t IO_ALIAS_BASE = 0x50000000;

    /** Groups of cascaded interrupt sources, encoded in the IRQ ID. */
    enum : uint64_t {
        CPU_INT_SHIFT  = 0,  // 68k-style autovector levels
        VIA2_INT_SHIFT = 8,  // VIA2 interrupt flag register
        SLOT_INT_SHIFT = 16, // VIA2 slot interrupt flag register
        F108_INT_SHIFT = 24, // F108 interrupt flag register
        ESCC_INT_SHIFT = 32, // the two SCC channels share one interrupt line
    };

    /** Autovector interrupt levels driven towards the CPU. */
    enum : uint8_t {
        CPU_INT_VIA1 = 1 << 1, // level 1: VIA1 (Cuda, 60.15 Hz tick, timers)
        CPU_INT_VIA2 = 1 << 2, // level 2: VIA2 (SCSI, sound, slots, F108)
        CPU_INT_ESCC = 1 << 4, // level 4: serial
        CPU_INT_NMI  = 1 << 7, // level 7: non-maskable interrupt
    };

    /** Apple Sound Chip register offsets, relative to the 0x14000 page.
        PrimeTime II holds the sound control logic and the FIFOs; DFAC II does
        the actual conversion. */
    enum : uint32_t {
        ASC_FIFO_A      = 0x000,
        ASC_FIFO_B      = 0x400,
        ASC_VERSION     = 0x800,
        ASC_MODE        = 0x801,
        ASC_CONTROL     = 0x802,
        ASC_FIFO_MODE   = 0x803,
        ASC_FIFO_STAT   = 0x804,
        ASC_VOLUME      = 0x806,
        ASC_CLOCK_RATE  = 0x807,
    };

    /** FIFO status bits, as tested by the ROM's chime routine. */
    enum : uint8_t {
        ASC_FIFO_A_HALF = 1 << 2, // FIFO A has room for more samples
        ASC_FIFO_B_HALF = 1 << 3, // ditto for FIFO B
    };

    /** VIA2 interrupt flag register bits. */
    enum : uint8_t {
        VIA2_INT_SCSI_DRQ = 1 << 0, // SCSI DRQ
        VIA2_INT_ALL_SLOT = 1 << 1, // any slot interrupt
        VIA2_INT_SCSI_IRQ = 1 << 3, // SCSI IRQ
        VIA2_INT_SOUND    = 1 << 4, // sound
        // MkLinux labels bit 5 "any button". PrimeTime II contains the SWIM II,
        // and AMIC puts the floppy controller on the same bit, so use it here.
        VIA2_INT_SWIM3    = 1 << 5,
        VIA2_INT_IRQ      = 1 << 7, // any enabled VIA2 interrupt
    };

    /** VIA2 slot interrupt flag register bits. CAUTION: reverse logic. */
    enum : uint8_t {
        SLOT_INT_F108   = 1 << 0, // cascaded F108 interrupt output
        SLOT_INT_PDS    = 1 << 5, // slot E (processor-direct slot)
        SLOT_INT_VIDEO  = 1 << 6, // video
    };

    /** F108 interrupt flag register bits. Writing "1" clears a flag. */
    enum : uint8_t {
        F108_INT_ENABLE = 3 << 0, // bits 0-1 enable rather than flag
        F108_INT_IR     = 1 << 2, // infrared receiver
        F108_INT_CC     = 1 << 3, // closed captioning
        F108_INT_IDE1   = 1 << 4, // ATA1
        F108_INT_IDE0   = 1 << 5, // ATA0
        F108_INT_VBL    = 1 << 6, // vertical blanking ("Keystone")
        F108_INT_IRQ    = 1 << 7, // F108 interrupt output
    };
}; // namespace PrimeTime

class PrimeTimeTwo : public MMIODevice, public InterruptCtrl {
public:
    PrimeTimeTwo();
    ~PrimeTimeTwo();

    static std::unique_ptr<HWComponent> create() {
        return std::unique_ptr<PrimeTimeTwo>(new PrimeTimeTwo());
    }

    // HWComponent methods
    int device_postinit() override;

    // MMIODevice methods
    uint32_t read(uint32_t rgn_start, uint32_t offset, int size) override;
    void write(uint32_t rgn_start, uint32_t offset, uint32_t value, int size) override;

    // InterruptCtrl methods
    uint64_t register_dev_int(IntSrc src_id) override;
    uint64_t register_dma_int(IntSrc src_id) override;
    void ack_int(uint64_t irq_id, uint8_t irq_line_state) override;
    void ack_dma_int(uint64_t irq_id, uint8_t irq_line_state) override;

    /** Called for writes to Capella's interrupt acknowledge register. */
    void clear_cpu_int();

    /** Called when the nanokernel reads Capella's interrupt status register,
        i.e. when it observes the currently pending 68k interrupt level. */
    void note_int_level_read() { this->int_since_read = false; }

    /** Highest pending 68k interrupt priority level, or zero when idle. */
    uint8_t get_int_level() const;

private:
    uint32_t via2_read(uint32_t offset);
    void     via2_write(uint32_t offset, uint8_t value);
    uint32_t f108_read(uint32_t offset);
    void     f108_write(uint32_t offset, uint8_t value);
    uint32_t asc_read(uint32_t offset);
    void     asc_write(uint32_t offset, uint8_t value);

    void ack_cpu_int(uint8_t level_mask, uint8_t irq_line_state);
    void ack_escc_int(uint8_t channel, uint8_t irq_line_state);
    void ack_via2_int(uint8_t via2_int, uint8_t irq_line_state);
    void ack_slot_int(uint8_t slot_int, uint8_t irq_line_state);
    void ack_f108_int(uint8_t f108_int, uint8_t irq_line_state);
    void update_via2_irq();
    void update_f108_irq();

    ViaCuda*            viacuda = nullptr;
    EsccController*     escc    = nullptr;
    Sc53C94*            scsi    = nullptr;
    Swim3::Swim3Ctrl*   swim3   = nullptr;
    IdeChannel*         ide0    = nullptr;

    uint8_t     via2_regs[16] = {};
    uint8_t     via2_ifr      = 0;
    uint8_t     via2_ier      = 0;
    uint8_t     slot_ifr      = 0x7F; // reverse logic: all lines idle
    uint8_t     slot_ier      = 0;
    uint8_t     f108_ifr      = 0;
    uint8_t     asc_regs[0x800] = {};

    // PrimeTime II integrates several sub-blocks whose register layouts are
    // internal to the custom IC and undocumented - an aux VIA-like block at
    // 0x1E000/0x1F000, video/DMA channel registers at 0x0E000, a command
    // sub-controller at 0x0A000 and GPIO lines at 0x18000. The ROM's power-on
    // self test writes each and reads it back to confirm the block is alive,
    // then bails into the serial debugger (MicroBug) if the read does not
    // match; MkLinux itself never touches any of them. We don't model these
    // blocks, so back the otherwise-unmapped I/O window with a small register
    // file: that satisfies the write/read-back check without pretending to be
    // any particular device (returning 0 or a floating 0xFF fails POST).
    std::unordered_map<uint32_t, uint8_t> io_stub_regs;
    std::unordered_map<uint32_t, bool>    io_stub_warned;
    uint8_t     escc_irq_lines = 0;
    uint8_t     cpu_int_lines = 0;
    uint8_t     acked_level   = 0; // priority level the nanokernel last read

    // Set whenever a source asserts an interrupt, cleared when the nanokernel
    // reads the pending level. If a source asserts while the 68k handler is
    // running - MkLinux chains the next disk command from inside its own
    // interrupt handler, and the drive raises the next INTRQ before the handler
    // acknowledges Capella - the level can be unchanged at acknowledge time and
    // the new interrupt would otherwise never be delivered. This flag lets
    // clear_cpu_int re-assert the 68k interrupt for that case.
    bool        int_since_read = false;

    uint8_t     via2_irq      = 0;
    uint8_t     slot_irq      = 0;
    uint8_t     f108_irq      = 0;
    bool        cpu_irq       = false;

    uint32_t    pseudo_vbl_tid = 0;
};

#endif // PRIME_TIME_TWO_H
