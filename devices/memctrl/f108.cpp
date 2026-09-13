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

/** F108 memory controller emulation. */

#include <devices/deviceregistry.h>
#include <devices/ioctrl/primetime.h>
#include <devices/memctrl/f108.h>
#include <machines/machinebase.h>

#include <cinttypes>
#include <loguru.hpp>

F108::F108() : MemCtrlBase()
{
    this->set_name("F108");

    supports_types(HWCompType::MEM_CTRL | HWCompType::MMIO_DEV);

    // add the register window of the Capella bus translation IC
    this->add_mmio_region(Capella::REG_BASE, Capella::REG_SIZE, this);

    // back the L2 cache data and tag windows
    this->add_ram_region(Capella::L2_DATA_BASE, Capella::L2_DATA_SIZE);
    this->add_ram_region(Capella::L2_TAG_BASE, Capella::L2_TAG_SIZE);
}

int F108::device_postinit()
{
    this->prime_time = dynamic_cast<PrimeTimeTwo*>(
        gMachineObj->get_comp_by_type(HWCompType::INT_CTRL));

    return 0;
}

uint32_t F108::read(uint32_t rgn_start, uint32_t offset, int size)
{
    return this->capella_regs[(offset >> 2) & 0x3F];
}

void F108::write(uint32_t rgn_start, uint32_t offset, uint32_t value, int size)
{
    this->capella_regs[(offset >> 2) & 0x3F] = value;

    if (offset == Capella::INT_ACK || offset == Capella::INT_ACK_2) {
        this->prime_time->clear_cpu_int();
        return;
    }

    LOG_F(9, "%s: Capella register write @%X = %X", this->name.c_str(),
          Capella::REG_BASE + offset, value);
}

int F108::install_ram(uint32_t bank1_size, uint32_t bank2_size)
{
    // Both SIMM sockets take 4 or 8 MB modules built from 4 Mbit devices, or
    // 16 or 32 MB modules built from 16 Mbit devices. Nothing is soldered on
    // the logic board, so the first socket must be populated.
    for (uint32_t size : {bank1_size, bank2_size}) {
        switch (size) {
        case 0: case 4: case 8: case 16: case 32:
            break;
        default:
            LOG_F(ERROR, "%s: unsupported SIMM size %d MB", this->name.c_str(), size);
            return -1;
        }
    }

    if (!bank1_size) {
        LOG_F(ERROR, "%s: the first RAM SIMM socket must be populated",
              this->name.c_str());
        return -1;
    }

    // The two SIMMs appear as one contiguous block starting at zero.
    uint32_t total_ram = (bank1_size + bank2_size) << 20;

    this->dram_ptr = std::unique_ptr<uint8_t[]> (new uint8_t[total_ram]());
    if (!this->dram_ptr) {
        LOG_F(ERROR, "%s: could not allocate RAM storage", this->name.c_str());
        return -1;
    }

    if (!this->add_ram_region(0, total_ram, this->dram_ptr.get())) {
        LOG_F(ERROR, "%s: could not map RAM", this->name.c_str());
        return -1;
    }

    LOG_F(INFO, "%s: %d MB of RAM installed", this->name.c_str(), total_ram >> 20);
    return 0;
}

static const DeviceDescription F108_Descriptor = {
    F108::create, {}, {}, HWCompType::MEM_CTRL | HWCompType::MMIO_DEV
};

REGISTER_DEVICE(F108, F108_Descriptor);
