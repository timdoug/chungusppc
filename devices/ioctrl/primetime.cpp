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

/** PrimeTime II I/O controller emulation. */

#include <core/timermanager.h>
#include <cpu/ppc/ppcemu.h>
#include <devices/common/ata/atadefs.h>
#include <devices/common/ata/idechannel.h>
#include <devices/common/hwcomponent.h>
#include <devices/common/scsi/sc53c94.h>
#include <devices/common/viacuda.h>
#include <devices/deviceregistry.h>
#include <devices/floppy/swim3.h>
#include <devices/ioctrl/primetime.h>
#include <devices/memctrl/memctrlbase.h>
#include <devices/serial/escc.h>
#include <machines/machinebase.h>

#include <cinttypes>
#include <loguru.hpp>
#include <memory>

using namespace PrimeTime;

PrimeTimeTwo::PrimeTimeTwo() : MMIODevice()
{
    this->set_name("PrimeTime II");

    supports_types(HWCompType::MMIO_DEV | HWCompType::INT_CTRL);

    this->viacuda = dynamic_cast<ViaCuda*>(gMachineObj->get_comp_by_name("ViaCuda"));
    this->escc    = dynamic_cast<EsccController*>(gMachineObj->get_comp_by_name("Escc"));
    this->scsi    = dynamic_cast<Sc53C94*>(gMachineObj->get_comp_by_name("Sc53C94"));
    this->swim3   = dynamic_cast<Swim3::Swim3Ctrl*>(gMachineObj->get_comp_by_name("Swim3"));
    this->ide0    = dynamic_cast<IdeChannel*>(gMachineObj->get_comp_by_name("Ide0"));

    // These machines have no DMA engine: the CPU moves SCSI data itself through
    // the 53C96's pseudo-DMA port, so no DMA channel is attached here.
    this->scsi->set_drq_callback([this](const uint8_t drq_state) {
        this->ack_via2_int(VIA2_INT_SCSI_DRQ, drq_state & 1);
    });
}

PrimeTimeTwo::~PrimeTimeTwo()
{
    if (this->pseudo_vbl_tid) {
        TimerManager::get_instance()->cancel_timer(this->pseudo_vbl_tid);
        this->pseudo_vbl_tid = 0;
    }
}

int PrimeTimeTwo::device_postinit()
{
    MemCtrlBase *mem_ctrl = dynamic_cast<MemCtrlBase *>
                           (gMachineObj->get_comp_by_type(HWCompType::MEM_CTRL));

    if (!mem_ctrl->add_mmio_region(IO_BASE, IO_SIZE, this) ||
        !mem_ctrl->add_mmio_region(IO_ALIAS_BASE, IO_SIZE, this)) {
        LOG_F(ERROR, "%s: couldn't register I/O region!", this->name.c_str());
        return -1;
    }

    // drive the VIA1 CA1 input to generate the 60.15 Hz tick
    this->pseudo_vbl_tid = TimerManager::get_instance()->add_cyclic_timer(
        static_cast<uint64_t>((1.0f/60.15) * NS_PER_SEC + 0.5f),
        [this]() {
            this->viacuda->assert_ctrl_line(ViaLine::CA1);
        });

    return 0;
}

uint32_t PrimeTimeTwo::read(uint32_t rgn_start, uint32_t offset, int size)
{
    switch (offset >> 12) {
    case 0x00: // VIA1 registers, 0x200 bytes apart
    case 0x01:
        return this->viacuda->read(offset >> 9);
    case 0x02: // VIA2 registers, likewise
    case 0x03:
        return this->via2_read(offset);
    // The SCC appears at the classic 68k read/write bases as well as at the
    // 0xC000 alias MkLinux uses. All of them reach the same chip.
    case 0x04:
    case 0x06:
    case 0x0C:
    case 0x0D:
        if ((offset & 0xF) < 0x0C)
            return this->escc->read(compat_to_macrisc[(offset >> 1) & 0xF]);
        LOG_F(ERROR, "%s: SCC read @%x.%c", this->name.c_str(), offset, SIZE_ARG(size));
        return 0;
    case 0x10: // SCSI registers, 16 bytes apart
    case 0x11:
        if (offset & 0x100)
            return this->scsi->pseudo_dma_read(size);
        return this->scsi->read((offset >> 4) & 0xF);
    case 0x14: // sound buffers and DFAC II control
        return this->asc_read(offset);
    case 0x16: // SWIM II registers, 0x200 bytes apart
    case 0x17:
        return this->swim3->read((offset >> 9) & 0xF);
    case 0x1A: // IDE channels and the F108 interrupt flags
        if ((offset & 0xFFF) >= 0x100)
            return this->f108_read(offset);
        if (offset & 0x80) // second channel: absent on these machines
            return 0xFF;
        {
            uint8_t reg = (offset & 0x7F) >> 2;
            if (reg == 0x0E) // 0x38: alternate status
                reg = ata_interface::ATA_Reg::ALT_STATUS;
            return this->ide0->read(reg, size);
        }
    }

    LOG_F(WARNING, "%s: unknown register read @%x.%c from PC=0x%08x",
          this->name.c_str(), rgn_start + offset, SIZE_ARG(size), ppc_state.pc);
    return 0;
}

void PrimeTimeTwo::write(uint32_t rgn_start, uint32_t offset, uint32_t value, int size)
{
    switch (offset >> 12) {
    case 0x00: // VIA1 registers
    case 0x01:
        this->viacuda->write(offset >> 9, value);
        return;
    case 0x02: // VIA2 registers
    case 0x03:
        this->via2_write(offset, value);
        return;
    case 0x04:
    case 0x06:
    case 0x0C:
    case 0x0D:
        if ((offset & 0xF) < 0x0C)
            this->escc->write(compat_to_macrisc[(offset >> 1) & 0xF], value);
        else
            LOG_F(ERROR, "%s: SCC write @%x.%c = %0*x", this->name.c_str(),
                  offset, SIZE_ARG(size), size * 2, value);
        return;
    case 0x10: // SCSI registers
    case 0x11:
        if (offset & 0x100)
            this->scsi->pseudo_dma_write(value, size);
        else
            this->scsi->write((offset >> 4) & 0xF, value);
        return;
    case 0x14: // sound buffers and DFAC II control
        this->asc_write(offset, value);
        return;
    case 0x16: // SWIM II registers
    case 0x17:
        this->swim3->write((offset >> 9) & 0xF, value);
        return;
    case 0x1A: // IDE channels and the F108 interrupt flags
        if ((offset & 0xFFF) >= 0x100) {
            this->f108_write(offset, value);
            return;
        }
        if (offset & 0x80)
            return;
        {
            uint8_t reg = (offset & 0x7F) >> 2;
            if (reg == 0x0E) // 0x38: device control
                reg = ata_interface::ATA_Reg::DEV_CTRL;
            this->ide0->write(reg, value, size);
        }
        return;
    }

    LOG_F(WARNING, "%s: unknown register write @%x.%c = %0*x from PC=0x%08x",
          this->name.c_str(), rgn_start + offset, SIZE_ARG(size), size * 2, value,
          ppc_state.pc);
}

// ============================== VIA2 registers ==============================
uint32_t PrimeTimeTwo::via2_read(uint32_t offset)
{
    switch ((offset >> 9) & 0xF) {
    case VIA_IFR:
        return this->via2_ifr;
    case VIA_IER:
        return this->via2_ier | 0x80;
    case 0xF: // slot interrupt flags, reverse logic
        return this->slot_ifr;
    default:
        return this->via2_regs[(offset >> 9) & 0xF];
    }
}

void PrimeTimeTwo::via2_write(uint32_t offset, uint8_t value)
{
    switch ((offset >> 9) & 0xF) {
    case VIA_IFR:
        // Writing a one to a flag clears it. Bit 7 is the read-only summary,
        // not the set/clear selector the IER uses: MkLinux's slot handler
        // dismisses its interrupt with a plain 0x02, and its interrupt setup
        // clears the register with 0x7F.
        this->via2_ifr &= ~(value & 0x7F);
        this->update_via2_irq();
        break;
    case VIA_IER:
        if (value & 0x80)
            this->via2_ier |= value & 0x7F;
        else
            this->via2_ier &= ~value;
        this->update_via2_irq();
        break;
    case 0xF: // slot interrupt enables
        if (value & 0x80)
            this->slot_ier |= value & 0x7F;
        else
            this->slot_ier &= ~value;
        this->ack_slot_int(0, 0); // re-evaluate with the new mask
        break;
    default:
        this->via2_regs[(offset >> 9) & 0xF] = value;
    }
}

// ============================ F108 interrupts ===============================
uint32_t PrimeTimeTwo::f108_read(uint32_t offset)
{
    if ((offset & 0xFFF) == 0x101)
        return this->f108_ifr;

    LOG_F(WARNING, "%s: unknown F108 register read @%x", this->name.c_str(),
          IO_BASE + offset);
    return 0;
}

void PrimeTimeTwo::f108_write(uint32_t offset, uint8_t value)
{
    // MkLinux's drive interrupt handler writes the source's bit here and then
    // zero, which is how it dismisses the interrupt: nothing else ever clears
    // the flag, and leaving it set holds the F108's output down for good.
    if ((offset & 0xFFF) == 0x100) {
        this->f108_ifr &= ~value;
        this->update_f108_irq();
        return;
    }

    if ((offset & 0xFFF) == 0x101) {
        // bits 0-1 are enables; writing "1" to bits 2-7 clears that flag
        this->f108_ifr = (this->f108_ifr & ~(value & ~F108_INT_ENABLE)) |
                         (value & F108_INT_ENABLE);
        this->update_f108_irq();
        return;
    }

    LOG_F(WARNING, "%s: unknown F108 register write @%x = %x", this->name.c_str(),
          IO_BASE + offset, value);
}

// ============================== Sound registers =============================
// Playback is not implemented. The registers are modelled far enough for the
// ROM's startup chime routine to run to completion instead of spinning on the
// FIFO status forever.
uint32_t PrimeTimeTwo::asc_read(uint32_t offset)
{
    uint32_t reg = offset & 0xFFF;

    if (reg < ASC_VERSION) // FIFO A and FIFO B data
        return 0;

    switch (reg) {
    case ASC_VERSION:
        return 0; // the original Apple Sound Chip
    case ASC_FIFO_STAT:
        // Both FIFOs always have room, so samples drain as fast as they arrive.
        return ASC_FIFO_A_HALF | ASC_FIFO_B_HALF;
    default:
        return this->asc_regs[reg - ASC_VERSION];
    }
}

void PrimeTimeTwo::asc_write(uint32_t offset, uint8_t value)
{
    uint32_t reg = offset & 0xFFF;

    if (reg < ASC_VERSION) // samples written to the FIFOs are discarded
        return;

    this->asc_regs[reg - ASC_VERSION] = value;
}

// ======================== Interrupt related stuff ===========================
uint64_t PrimeTimeTwo::register_dev_int(IntSrc src_id)
{
    switch (src_id) {
    case IntSrc::VIA_CUDA  : return uint64_t(CPU_INT_VIA1) << CPU_INT_SHIFT;
    case IntSrc::NMI       : return uint64_t(CPU_INT_NMI)  << CPU_INT_SHIFT;

    // The SCC in the F108 has a single interrupt output, but the driver may
    // register each channel separately, so track them independently.
    case IntSrc::ESCC      : return uint64_t(3) << ESCC_INT_SHIFT;
    case IntSrc::SCCA      : return uint64_t(1) << ESCC_INT_SHIFT;
    case IntSrc::SCCB      : return uint64_t(2) << ESCC_INT_SHIFT;

    case IntSrc::SCSI_CURIO: return uint64_t(VIA2_INT_SCSI_IRQ) << VIA2_INT_SHIFT;
    case IntSrc::DAVBUS    : return uint64_t(VIA2_INT_SOUND)    << VIA2_INT_SHIFT;
    case IntSrc::SWIM3     : return uint64_t(VIA2_INT_SWIM3)    << VIA2_INT_SHIFT;
    case IntSrc::SLOT_ALL  : return uint64_t(VIA2_INT_ALL_SLOT) << VIA2_INT_SHIFT;

    case IntSrc::SLOT_PDS  : return uint64_t(SLOT_INT_PDS)   << SLOT_INT_SHIFT;

    case IntSrc::IDE0      : return uint64_t(F108_INT_IDE0) << F108_INT_SHIFT;
    case IntSrc::IDE1      : return uint64_t(F108_INT_IDE1) << F108_INT_SHIFT;
    // Valkyrie drives the VIA2 slot register's video line. Putting it on the
    // F108's "Keystone" flag instead, which MkLinux registers its own VBL
    // handler on, makes Mac OS stop with "unserviceable slot interrupt": the
    // F108 output arrives as slot 0 and nothing claims it.
    case IntSrc::VALKYRIE  : return uint64_t(SLOT_INT_VIDEO) << SLOT_INT_SHIFT;
    default:
        ABORT_F("%s: unknown interrupt source %d", this->name.c_str(), src_id);
    }
    return 0;
}

uint64_t PrimeTimeTwo::register_dma_int(IntSrc src_id)
{
    ABORT_F("%s: this machine has no DMA engine", this->name.c_str());
    return 0;
}

void PrimeTimeTwo::ack_int(uint64_t irq_id, uint8_t irq_line_state)
{
    if (irq_id >> ESCC_INT_SHIFT) {
        this->ack_escc_int(irq_id >> ESCC_INT_SHIFT, irq_line_state);
    } else if (irq_id >> F108_INT_SHIFT) {
        this->ack_f108_int(irq_id >> F108_INT_SHIFT, irq_line_state);
    } else if (irq_id >> SLOT_INT_SHIFT) {
        this->ack_slot_int(irq_id >> SLOT_INT_SHIFT, irq_line_state);
    } else if (irq_id >> VIA2_INT_SHIFT) {
        this->ack_via2_int(irq_id >> VIA2_INT_SHIFT, irq_line_state);
    } else if (irq_id >> CPU_INT_SHIFT) {
        this->ack_cpu_int(irq_id >> CPU_INT_SHIFT, irq_line_state);
    } else {
        ABORT_F("%s: unknown interrupt source ID 0x%llX", this->name.c_str(),
                (unsigned long long)irq_id);
    }
}

void PrimeTimeTwo::ack_dma_int(uint64_t irq_id, uint8_t irq_line_state)
{
    ABORT_F("%s: this machine has no DMA engine", this->name.c_str());
}

void PrimeTimeTwo::ack_escc_int(uint8_t channel, uint8_t irq_line_state)
{
    if (irq_line_state)
        this->escc_irq_lines |= channel;
    else
        this->escc_irq_lines &= ~channel;
    this->ack_cpu_int(CPU_INT_ESCC, !!this->escc_irq_lines);
}

void PrimeTimeTwo::ack_f108_int(uint8_t f108_int, uint8_t irq_line_state)
{
    if (irq_line_state)
        this->f108_ifr |= f108_int;
    else
        this->f108_ifr &= ~f108_int;
    this->update_f108_irq();
}

void PrimeTimeTwo::update_f108_irq()
{
    uint8_t flags   = this->f108_ifr & ~(F108_INT_ENABLE | F108_INT_IRQ);
    uint8_t new_irq = !!(flags && (this->f108_ifr & F108_INT_ENABLE));
    this->f108_ifr = (this->f108_ifr & ~F108_INT_IRQ) | (new_irq ? F108_INT_IRQ : 0);
    if (new_irq != this->f108_irq) {
        this->f108_irq = new_irq;
        // the F108 interrupt output is cascaded into the VIA2 slot register
        this->ack_slot_int(SLOT_INT_F108, new_irq);
    }
}

void PrimeTimeTwo::ack_slot_int(uint8_t slot_int, uint8_t irq_line_state)
{
    // CAUTION: reverse logic (0 - asserted, 1 - idle) in the slot IFR!
    if (irq_line_state)
        this->slot_ifr &= ~slot_int;
    else
        this->slot_ifr |= slot_int;

    // Any asserted slot line raises VIA2's "any slot" flag; the masking that
    // matters is VIA2's own IER. There is no per-slot enable to consult -
    // MkLinux never writes one, and its handler reaches the cascaded F108 by
    // way of VIA2 bit 1 alone.
    uint8_t new_irq = !!(~this->slot_ifr & 0x7F);
    if (new_irq != this->slot_irq) {
        this->slot_irq = new_irq;
        this->ack_via2_int(VIA2_INT_ALL_SLOT, new_irq);
    }
}

void PrimeTimeTwo::ack_via2_int(uint8_t via2_int, uint8_t irq_line_state)
{
    if (irq_line_state)
        this->via2_ifr |= via2_int;
    else
        this->via2_ifr &= ~via2_int;
    this->update_via2_irq();
}

void PrimeTimeTwo::update_via2_irq()
{
    uint8_t new_irq = !!(this->via2_ifr & this->via2_ier & 0x7F);
    this->via2_ifr  = (this->via2_ifr & 0x7F) | (new_irq << 7);
    if (new_irq != this->via2_irq) {
        this->via2_irq = new_irq;
        this->ack_cpu_int(CPU_INT_VIA2, new_irq);
    }
}

// Capella requests a 603 interrupt whenever the 68k priority level changes
// away from the one the nanokernel last acknowledged - including when it falls
// back to idle. The nanokernel's handler at 0x403158A8 acknowledges Capella,
// reads the level from the +0x24 register and stores it as the interrupt
// priority level of the emulated 68k; that store is the only thing that ever
// lowers it. Asserting only on a rising edge therefore leaves the 68k believing
// an interrupt is forever pending, and its autovector handler re-enters itself
// as soon as each rte restores the unmasked status register.
void PrimeTimeTwo::ack_cpu_int(uint8_t level_mask, uint8_t irq_line_state)
{
    if (irq_line_state)
        this->cpu_int_lines |= level_mask;
    else
        this->cpu_int_lines &= ~level_mask;

    if (this->get_int_level() != this->acked_level && !this->cpu_irq) {
        this->cpu_irq = true;
        ppc_assert_int();
        LOG_F(9, "%s: CPU INT asserted, levels: 0x%02x", this->name.c_str(),
              this->cpu_int_lines);
    }
}

// Capella reports this level to the ROM, which hands it to the 68k emulator as
// the interrupt priority level. cpu_int_lines uses one bit per level.
uint8_t PrimeTimeTwo::get_int_level() const
{
    for (int level = 7; level > 0; level--) {
        if (this->cpu_int_lines & (1 << level))
            return level;
    }
    return 0;
}

// The nanokernel reads the priority level right after this acknowledge, so the
// level captured here is the one it is about to see.
void PrimeTimeTwo::clear_cpu_int()
{
    this->acked_level = this->get_int_level();

    if (this->cpu_irq) {
        this->cpu_irq = false;
        ppc_release_int();
        LOG_F(9, "%s: CPU INT acknowledged at level %d", this->name.c_str(),
              this->acked_level);
    }
}

// ========================= Device registry stuff ============================
static std::vector<std::string> PrimeTime_Subdevices = {
    "Sc53C94", "Escc", "ViaCuda", "Swim3", "Ide0"
};

static const DeviceDescription PrimeTime_Descriptor = {
    PrimeTimeTwo::create, PrimeTime_Subdevices, {},
    HWCompType::MMIO_DEV | HWCompType::INT_CTRL
};

REGISTER_DEVICE(PrimeTimeTwo, PrimeTime_Descriptor);
