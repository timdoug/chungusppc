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

/** Highspeed Memory Controller emulation.

    Author: Max Poliakovski
*/

#include <devices/deviceregistry.h>
#include <devices/common/hwcomponent.h>
#include <devices/memctrl/hmc.h>
#include <loguru.hpp>

HMC::HMC() : MemCtrlBase()
{
    this->name = "Highspeed Memory Controller";

    supports_types(HWCompType::MEM_CTRL | HWCompType::MMIO_DEV);

    // add memory mapped I/O region for the HMC control register
    this->add_mmio_region(0x50F40000, 0x10000, this);

    this->ctrl_reg = 0ULL;
    this->bit_pos = 0;
}

uint32_t HMC::read(uint32_t rgn_start, uint32_t offset, int size)
{
    if (offset) return 0;
    uint32_t value = (ctrl_reg >> bit_pos) & 1;
    bit_pos = (bit_pos + 1) % HMC_CTRL_BITS;
    return value;
}

void HMC::write(uint32_t rgn_start, uint32_t offset, uint32_t value, int size)
{
    uint64_t bit;

    switch(offset) {
        case 0:
            bit = 1ULL << this->bit_pos++;
            this->ctrl_reg = (value & 1) ?
                this->ctrl_reg | bit
            :
                this->ctrl_reg & ~bit;
            if (this->bit_pos >= HMC_CTRL_BITS) {
                this->bit_pos = 0;
                uint8_t new_bank_config = (this->ctrl_reg >> HMC_RAM_CFG_POS) & 3;
                if (new_bank_config != this->bank_config) {
                    this->bank_config = new_bank_config;
                    this->remap_ram_regions();
                }
            }
            break;
        case 8: // writing to HMCBase + 8 resets internal bit position
            this->bit_pos = 0;
            break;
    }
}

void HMC::remap_ram_regions() {
    if (fixed_ram_layout) {
        if (bank_config != BANK_CFG_128MB)
            LOG_F(WARNING, "%s: compact RAM matrix %u is not implemented on this board",
                  name.c_str(), bank_config);
        return;
    }
    uint32_t bank_b_addr;

    switch (this->bank_config) {
    case BANK_CFG_128MB:
        bank_b_addr = BANK_B_START;
        break;
    case BANK_CFG_2MB:
        bank_b_addr = this->mb_bank_size + BANK_SIZE_2MB;
        break;
    case BANK_CFG_8MB:
        bank_b_addr = this->mb_bank_size + BANK_SIZE_8MB;
        break;
    case BANK_CFG_32MB:
        bank_b_addr = this->mb_bank_size + BANK_SIZE_32MB;
        break;
    }

    if (this->bank_b_size && this->bank_b_start != bank_b_addr) {
        AddressMapEntry *ref_entry = find_range(this->bank_b_start);
        if (ref_entry) {
            ref_entry->end   = bank_b_addr + (ref_entry->end - ref_entry->start);
            ref_entry->start = bank_b_addr;

            this->bank_b_start = bank_b_addr;
            LOG_F(INFO, "%s: successfully relocated bank B mem region to 0x%X",
                  this->name.c_str(), bank_b_addr);
        } else
            LOG_F(ERROR, "%s: failed to relocate bank B mem region to 0x%X",
                  this->name.c_str(), bank_b_addr);
    }
}

int HMC::install_ram(uint32_t mb_bank_size, uint32_t bank_a_size, uint32_t bank_b_size) {
    if (mb_bank_size != BANK_SIZE_4MB && mb_bank_size != BANK_SIZE_8MB) {
        LOG_F(ERROR, "%s: invalid motherboard bank size %d", this->name.c_str(),
              mb_bank_size);
        return -1;
    }

    if (!bank_a_size && bank_b_size) {
        LOG_F(ERROR, "%s: bank A can't be empty while bank B is not empty",
              this->name.c_str());
        return -1;
    }

    if (!this->add_ram_region(BANK_MB_START, mb_bank_size)) {
        LOG_F(ERROR, "%s: could not allocate motherboard RAM region!", this->name.c_str());
        return -1;
    }

    this->mb_bank_start = BANK_MB_START;
    this->mb_bank_size  = mb_bank_size;
    this->bank_a_start  = -1;
    this->bank_a_size   = bank_a_size;
    this->bank_b_start  = -1;
    this->bank_b_size   = bank_b_size;

    if (bank_a_size) {
        // create alias for RAM bank A (required for memory sizing)
        if (!this->add_ram_region(BANK_A_ALIAS, bank_a_size)) {
            LOG_F(ERROR, "%s: could not allocate region for bank A!",
                  this->name.c_str());
            return -1;
        }

        this->bank_a_start = BANK_A_ALIAS;

        uint32_t offset = 0;
        uint32_t size   = bank_a_size;

        // make the main region for the bank A starting right after
        // the motherboard bank
        if (bank_a_size > BANK_SIZE_120MB) {
            // For a full 128MB bank, the lower part of this region is hidden
            // by the motherboard bank. Set up the partial mirror!
            offset  = mb_bank_size;
            size    = BANK_SIZE_120MB;
        }

        if (!this->add_mem_mirror_partial(mb_bank_size, BANK_A_ALIAS,
                                          offset, size)) {
            LOG_F(ERROR, "%s: could not create mirror for RAM bank A!",
                  this->name.c_str());
            return -1;
        }

        // Create additional aliases for bank A if the installed memory is
        // smaller than 8 MB. That's because HWInit always searches those areas
        // for the warm start signature.
        if (bank_a_size < BANK_SIZE_8MB) {
            for (uint32_t alias_start = BANK_B_START + bank_a_size - BANK_SIZE_8MB;
                alias_start < BANK_B_START; alias_start += bank_a_size
            ) {
                if (!this->add_mem_mirror(alias_start, this->bank_a_start)) {
                    LOG_F(ERROR, "%s: could not create alias for RAM bank A!",
                          this->name.c_str());
                    return -1;
                }
            }
        }
    }

    if (bank_b_size) {
        if (!this->add_ram_region(BANK_B_START, bank_b_size)) {
            LOG_F(ERROR, "%s: could not allocate region for bank B!",
                  this->name.c_str());
            return -1;
        }

        this->bank_b_start = BANK_B_START;

        // Create additional aliases for bank B if the installed memory is
        // smaller than 8 MB. That's because HWInit always searches those areas
        // for the warm start signature.
        if (bank_b_size < BANK_SIZE_8MB) {
            for (uint32_t alias_start = BANK_A_ALIAS + bank_b_size - BANK_SIZE_8MB;
                 alias_start < BANK_A_ALIAS; alias_start += bank_b_size) {
                if (!this->add_mem_mirror(alias_start, this->bank_b_start)) {
                    LOG_F(ERROR, "%s: could not create alias for RAM bank B!",
                          this->name.c_str());
                    return -1;
                }
            }
        }
    }

    this->remap_ram_regions();

    return 0;
}

int HMC::install_ram_banks(const std::vector<uint32_t>& banks) {
    if (banks.size() > 8) return -1;
    for (uint32_t size : banks) {
        if (size && (size < 0x00100000 || size > BANK_SIZE_32MB || (size & (size - 1)))) {
            LOG_F(ERROR, "%s: invalid RAM bank size %u", name.c_str(), size);
            return -1;
        }
    }
    fixed_ram_layout = true;
    if (!add_ram_region(BANK_MB_START, BANK_SIZE_8MB)) return -1;

    // Apple, Enhanced Power Macintosh developer note, Appendix A, Table A-2:
    // each expansion bank occupies up to 32 MiB, starting 16 MiB into its
    // 64 MiB physical window. These are independent banks, never aliases.
    // Mapping bank A again at 0x10000000 made the ROM report the same storage
    // twice; Mach then allocated overlapping pages and corrupted live data.
    for (size_t bank = 0; bank < banks.size(); ++bank) {
        if (banks[bank] && !add_ram_region(0x01000000 + bank * 0x04000000, banks[bank]))
            return -1;
    }
    return 0;
}

static const DeviceDescription Hmc_Descriptor = {
    HMC::create, {}, {}, HWCompType::MEM_CTRL | HWCompType::MMIO_DEV
};

REGISTER_DEVICE(HMC, Hmc_Descriptor);
