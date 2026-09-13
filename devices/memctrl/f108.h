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

/** @file F108 memory controller definitions.

    The F108 provides memory control for the Power Macintosh 5200/6200 family,
    along with the SCSI, SCC and IDE controllers. Only the memory control part
    lives here; the controllers it contains are decoded by PrimeTime II with
    the rest of the I/O page.

    This object also owns the register window of the Capella bus translation
    IC, which sits between the 603 and the 68040 bus and carries the interrupt
    acknowledge register.

    See the "Power Macintosh 5200/75 LC and 6200/75 Computers" developer note,
    pp. 16-22, for the chip descriptions and the address map.
 */

#ifndef F108_MEMCTRL_H
#define F108_MEMCTRL_H

#include <devices/common/hwcomponent.h>
#include <devices/common/mmiodevice.h>
#include <devices/memctrl/memctrlbase.h>

#include <cinttypes>
#include <memory>

class PrimeTimeTwo;

namespace Capella {
    constexpr uint32_t REG_BASE = 0x53000000;
    constexpr uint32_t REG_SIZE = 0x00001000;
    // MkLinux's interrupt_performa.c acknowledges interrupts at +0x18, while
    // the 5200/6200 ROM's handler at 0x40307358 uses +0x1C. Accept both.
    constexpr uint32_t INT_ACK    = 0x18;
    constexpr uint32_t INT_ACK_2  = 0x1C;
    constexpr uint32_t INT_STATUS = 0x24; // low three bits read by the handler

    // Capella exposes the 256 KB L2 cache on the ROM/cache DIMM through two
    // diagnostic windows, which the ROM's power-on test at 0x403089CC walks
    // after enabling them through the register at +0x0C. Backing them with
    // plain storage is enough: the emulator has no caches to keep coherent.
    constexpr uint32_t L2_DATA_BASE = 0x51000000;
    constexpr uint32_t L2_DATA_SIZE = 0x00040000;
    constexpr uint32_t L2_TAG_BASE  = 0x52000000;
    constexpr uint32_t L2_TAG_SIZE  = 0x00020000;
}; // namespace Capella

namespace F108Mem {
    constexpr uint32_t ROM_BASE     = 0x40000000; // 603 ROM space
    constexpr uint32_t ROM_SIZE     = 0x00400000; // 4 MB ROM on the ROM/cache DIMM
    constexpr uint32_t ROM_MIRROR   = 0xFFC00000; // ROM image the 603 starts from
}; // namespace F108Mem

class F108 : public MemCtrlBase, public MMIODevice {
public:
    F108();
    ~F108() = default;

    static std::unique_ptr<HWComponent> create() {
        return std::unique_ptr<F108>(new F108());
    }

    // HWComponent methods
    int device_postinit() override;

    // MMIODevice methods
    uint32_t read(uint32_t rgn_start, uint32_t offset, int size) override;
    void write(uint32_t rgn_start, uint32_t offset, uint32_t value, int size) override;

    /** Install the two RAM SIMMs, in megabytes. */
    int install_ram(uint32_t bank1_size, uint32_t bank2_size);

private:
    std::unique_ptr<uint8_t[]>  dram_ptr;
    PrimeTimeTwo*   prime_time = nullptr;
    uint32_t        capella_regs[64] = {};
};

#endif // F108_MEMCTRL_H
