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

/** @file Cordyceps machines (Power Macintosh 5200, Performa 6200).

    These are Quadra/LC 630 logic boards with a PowerPC 603 grafted on: a
    Capella bus translator bridges the 603 to a 68040 bus carrying the F108
    memory controller, the Valkyrie display controller and the PrimeTime II
    I/O bridge. There is no NuBus and no PCI, and no DMA engine either - the
    processor moves all I/O data itself.

    The hard disk is on the F108's IDE interface and the CD-ROM, if any, is on
    its SCSI bus. See the "Power Macintosh 5200/75 LC and 6200/75 Computers"
    developer note for the block diagram and address map.
 */

#include <cpu/ppc/ppcemu.h>
#include <devices/common/machineid.h>
#include <devices/deviceregistry.h>
#include <devices/memctrl/f108.h>
#include <loguru.hpp>
#include <machines/machine.h>
#include <machines/machinebase.h>
#include <machines/machinefactory.h>
#include <machines/machineproperties.h>

#include <string>

class MachineCordyceps : public Machine {
public:
    static std::unique_ptr<HWComponent> create5200() {
        return Machine::create_with_id<MachineCordyceps>("pm5200");
    }

    static std::unique_ptr<HWComponent> create6200() {
        return Machine::create_with_id<MachineCordyceps>("pm6200");
    }

    int initialize(const std::string &id);
};

int MachineCordyceps::initialize(const std::string &id) {
    LOG_F(INFO, "Building machine Cordyceps...");

    F108* f108_obj = dynamic_cast<F108*>(gMachineObj->get_comp_by_name("F108"));

    // allocate ROM region and mirror it where the 603 starts fetching
    if (!f108_obj->add_rom_region(F108Mem::ROM_BASE, F108Mem::ROM_SIZE)) {
        LOG_F(ERROR, "Could not allocate ROM region!");
        return -1;
    }

    if (!f108_obj->add_mem_mirror(F108Mem::ROM_MIRROR, F108Mem::ROM_BASE)) {
        LOG_F(ERROR, "Could not create ROM mirror!");
        return -1;
    }

    // install the two RAM SIMMs
    if (f108_obj->install_ram(GET_INT_PROP("rambank1_size"),
                              GET_INT_PROP("rambank2_size"))) {
        LOG_F(ERROR, "Failed to allocate RAM!");
        return -1;
    }

    // The ROM reads a machine ID from the same place the NuBus Power Macs use
    // and looks it up in the table of machine descriptors at ROM offset
    // 0x203DC, hanging at 0x1B8A8 when nothing matches. That table holds ten
    // entries for this family, 0x3250 through 0x325E; the low byte agrees with
    // MkLinux's model_dep.c, which expects 0x50 or 0x58 on the 75 MHz models
    // and 0x51 or 0x59 on the 80 MHz ones. Which entry is which model is not
    // yet known, so pick another with the machine_id property.
    gMachineObj->add_device("MachineID", std::unique_ptr<NubusMacID>(
        new NubusMacID(GET_INT_PROP("machine_id"))));
    f108_obj->add_mmio_region(0x5FFFFFFC, 4,
        dynamic_cast<MMIODevice*>(gMachineObj->get_comp_by_name("MachineID")));

    // The 603 runs at 75 MHz off a 25 MHz bus clock (MkLinux's model_dep.c);
    // its time base counts at a quarter of the bus clock.
    uint64_t bus_freq      = 25000000ULL;
    uint64_t timebase_freq = bus_freq / 4;

    // init virtual CPU and request a PowerPC 603
    ppc_cpu_init(f108_obj, PPC_VER::MPC603, false, timebase_freq);

    return 0;
}

static const PropMap pm5200_settings = {
    {"rambank1_size",
        new IntProperty(8, std::vector<uint32_t>({4, 8, 16, 32}))},
    {"rambank2_size",
        new IntProperty(0, std::vector<uint32_t>({0, 4, 8, 16, 32}))},
    {"machine_id",
        new IntProperty(0x3250)},
    {"hdd_config",
        new StrProperty("Ide0:0")},
    {"emmo",
        new BinProperty(0)},
};

// The hard disk is on IDE; a SCSI CD-ROM is attached with the cdr_img property
// of the 53C96, the same way the NuBus Power Macs do it.
static std::vector<std::string> pm5200_devices = {
    "F108", "PrimeTimeTwo", "ValkyrieCordyceps", "AtaHardDisk", "AtaHardDisk2"
};

static const DeviceDescription Machine5200_descriptor = {
    MachineCordyceps::create5200, pm5200_devices, pm5200_settings
};

static const DeviceDescription Machine6200_descriptor = {
    MachineCordyceps::create6200, pm5200_devices, pm5200_settings
};

REGISTER_DEVICE(MachineCordyceps5200, Machine5200_descriptor);
REGISTER_DEVICE(MachineCordyceps6200, Machine6200_descriptor);

static const MachineDescription pm5200_descriptor = {
    .name = "pm5200",
    .description = "Power Macintosh 5200",
    .machine_root = "MachineCordyceps5200"
};

static const MachineDescription pm6200_descriptor = {
    .name = "pm6200",
    .description = "Performa 6200",
    .machine_root = "MachineCordyceps6200"
};

REGISTER_MACHINE(pm5200, pm5200_descriptor);
REGISTER_MACHINE(pm6200, pm6200_descriptor);
