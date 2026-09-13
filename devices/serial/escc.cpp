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

/** @file Enhanced Serial Communications Controller (ESCC) emulation. */

#include <core/timermanager.h>
#include <devices/deviceregistry.h>
#include <devices/serial/chario.h>
#include <devices/serial/escc.h>
#include <devices/serial/z85c30.h>
#include <core/hostevents.h>
#include <core/timermanager.h>
#include <loguru.hpp>
#include <machines/machinebase.h>
#include <machines/machineproperties.h>

#include <cinttypes>
#include <memory>
#include <string>
#include <vector>

/** Remap the compatible addressing scheme to the MacRISC one. */
const uint8_t compat_to_macrisc[6] = {
    EsccReg::Port_B_Cmd,    EsccReg::Port_A_Cmd,
    EsccReg::Port_B_Data,   EsccReg::Port_A_Data,
    EsccReg::Enh_Reg_B,     EsccReg::Enh_Reg_A
};

EsccController::EsccController()
{
    // allocate channels
    this->ch_a = std::unique_ptr<EsccChannel> (new EsccChannel("ESCC_A"));
    this->ch_b = std::unique_ptr<EsccChannel> (new EsccChannel("ESCC_B"));
    ch_a->set_interrupt_callback([this] { update_interrupts(); });
    ch_b->set_interrupt_callback([this] { update_interrupts(); });

    // attach backends
    std::string backend_name = GET_STR_PROP("serial_backend");

    this->ch_a->attach_backend(
        (backend_name == "stdio") ? CHARIO_BE_STDIO :
#ifdef _WIN32
#else
        (backend_name == "socket") ? CHARIO_BE_SOCKET :
#endif
        CHARIO_BE_NULL
    );
    std::string backend_b = GET_STR_PROP("serial_b_backend");
    this->ch_b->attach_backend(
        (backend_b == "stdio") ? CHARIO_BE_STDIO :
#ifndef _WIN32
        (backend_b == "socket") ? CHARIO_BE_SOCKET :
#endif
        CHARIO_BE_NULL);

    this->master_int_cntrl = 0;
    this->reset();
}

void EsccController::reset()
{
    this->master_int_cntrl &= (WR9_NO_VECTOR | WR9_VECTOR_INCLUDES_STATUS);
    this->master_int_cntrl |= WR9_FORCE_HARDWARE_RESET;
    this->reg_ptr = WR0; // or RR0

    this->ch_a->reset(true);
    this->ch_b->reset(true);
}

int EsccController::device_postinit()
{
    int_ctrl = dynamic_cast<InterruptCtrl*>(gMachineObj->get_comp_by_type(HWCompType::INT_CTRL));
    if (gMachineObj->get_comp_by_name_optional("Amic")) {
        irq_a = irq_b = int_ctrl->register_dev_int(IntSrc::ESCC);
    } else {
        irq_a = int_ctrl->register_dev_int(IntSrc::SCCA);
        irq_b = int_ctrl->register_dev_int(IntSrc::SCCB);
    }
    EventManager::get_instance()->add_post_handler(this, &EsccController::poll);
    return 0;
}

void EsccController::poll()
{
    ch_a->poll_dma();
    ch_b->poll_dma();
    update_interrupts();
}

void EsccController::update_interrupts()
{
    if (!int_ctrl) return;
    bool enabled = master_int_cntrl & WR9_MASTER_INTERRUPT_ENABLE;
    bool a = enabled && ch_a->interrupt_pending();
    bool b = enabled && ch_b->interrupt_pending();
    if (irq_a == irq_b) {
        a = a || b;
        if (a != irq_level_a) int_ctrl->ack_int(irq_a, a);
        irq_level_a = a;
    } else {
        if (a != irq_level_a) int_ctrl->ack_int(irq_a, a);
        if (b != irq_level_b) int_ctrl->ack_int(irq_b, b);
        irq_level_a = a;
        irq_level_b = b;
    }
}

uint8_t EsccController::read(uint8_t reg_offset)
{
    uint8_t value;

    switch(reg_offset) {
    case EsccReg::Port_B_Cmd:
        value = this->read_internal(this->ch_b.get());
        break;
    case EsccReg::Port_A_Cmd:
        value = this->read_internal(this->ch_a.get());
        break;
    case EsccReg::Port_B_Data:
        value = this->ch_b->receive_byte();
        break;
    case EsccReg::Port_A_Data:
        value = this->ch_a->receive_byte();
        break;
    case EsccReg::Enh_Reg_B:
        value = this->ch_b->get_enh_reg();
        break;
    case EsccReg::Enh_Reg_A:
        value = this->ch_a->get_enh_reg();
        break;
    case LocalTalkReg::Rec_Count:
        value = this->recovery_counter;
        break;
    case LocalTalkReg::Start_A:
        value = this->start_a;
        break;
    case LocalTalkReg::Start_B:
        value = this->start_b;
        break;
    case LocalTalkReg::Detect_AB:
        value = this->detect_ab;
        break;
    default:
        LOG_F(WARNING, "%s: reading from unimplemented register 0x%x", this->name.c_str(),
              reg_offset);
        value = 0;
    }

    update_interrupts();
    return value;
}

void EsccController::write(uint8_t reg_offset, uint8_t value)
{
    switch(reg_offset) {
    case EsccReg::Port_B_Cmd:
        this->write_internal(this->ch_b.get(), value);
        break;
    case EsccReg::Port_A_Cmd:
        this->write_internal(this->ch_a.get(), value);
        break;
    case EsccReg::Port_B_Data:
        this->ch_b->send_byte(value);
        break;
    case EsccReg::Port_A_Data:
        this->ch_a->send_byte(value);
        break;
    case EsccReg::Enh_Reg_B:
        this->ch_b->set_enh_reg(value);
        break;
    case EsccReg::Enh_Reg_A:
        this->ch_a->set_enh_reg(value);
        break;
    case LocalTalkReg::Rec_Count:
        this->recovery_counter = value;
        LOG_F(INFO, "ESCC recovery counter set to 0x%X", this->recovery_counter);
        break;
    case LocalTalkReg::Start_A:
        this->start_a = value & 1;
        if (this->start_a) {
            this->detect_ab |= 2; // say we've just detected the Abort sequence
            this->ch_a->update_ltpc_eop_flag(1);
        } else {
            this->detect_ab &= ~2; // reset Abort detected flag
            this->ch_a->update_ltpc_eop_flag(0);
        }
        break;
    case LocalTalkReg::Start_B:
        this->start_b = value & 1;
        if (this->start_b) {
            this->detect_ab |= 1; // say we've just detected the Abort sequence
            this->ch_b->update_ltpc_eop_flag(1);
        } else {
            this->detect_ab &= ~1; // reset Abort detected flag
            this->ch_b->update_ltpc_eop_flag(0);
        }
        break;
    default:
        LOG_F(WARNING, "%s: writing 0x%X to unimplemented register 0x%x", this->name.c_str(),
              value, reg_offset);
    }
    update_interrupts();
}

uint8_t EsccController::read_internal(EsccChannel *ch)
{
    uint8_t value;
    switch (this->reg_ptr) {
    case RR3:
        value = ch == ch_a.get() ?
            (ch_a->interrupt_pending() << 3) | ch_b->interrupt_pending() : 0;
        break;
    case RR2:
        // TODO: implement interrupt vector modifications
        value = this->int_vec;
        break;
    default:
        value = ch->read_reg(this->reg_ptr);
    }
    this->reg_ptr = RR0; // or WR0
    return value;
}

void EsccController::write_internal(EsccChannel *ch, uint8_t value)
{
    switch (this->reg_ptr) {
    // chip-specific registers
    case WR0:
        this->reg_ptr = value & WR0_REGISTER_SELECTION_CODE;
        switch (value & WR0_COMMAND_CODES) {
        case WR0_COMMAND_POINT_HIGH:
            this->reg_ptr |= WR8; // or RR8
            break;
        default:
            ch->command(value);
            break;
        }
        return;
    case WR2:
        this->int_vec = value;
        break;
    case WR9:
        // see if some reset is requested
        switch (value & WR9_RESET_COMMAND_BITS) {
        case WR9_CHANNEL_RESET_B:
            this->master_int_cntrl &= ~WR9_INTERRUPT_MASKING_WITHOUT_INTACK;
            this->ch_b->reset(false);
            break;
        case WR9_CHANNEL_RESET_A:
            this->master_int_cntrl &= ~WR9_INTERRUPT_MASKING_WITHOUT_INTACK;
            this->ch_a->reset(false);
            break;
        case WR9_FORCE_HARDWARE_RESET:
            this->reset();
            break;
        }

        this->master_int_cntrl = value & WR9_INTERRUPT_CONTROL_BITS;
        break;
    default:
        // channel-specific registers
        ch->write_reg(this->reg_ptr, value);
    }
    this->reg_ptr = WR0; // or RR0
}

// ======================== ESCC Channel methods ==============================
EsccChannel::~EsccChannel()
{
    if (rx_timer) TimerManager::get_instance()->cancel_timer(rx_timer);
    if (tx_timer) TimerManager::get_instance()->cancel_timer(tx_timer);
}

uint64_t EsccChannel::character_period(bool transmit) const
{
    static const unsigned multipliers[] = {1, 16, 32, 64};
    uint64_t clocks = multipliers[write_regs[WR4] >> 6];
    unsigned clock_source = (write_regs[WR11] >> (transmit ? 3 : 5)) & 3;
    if (clock_source == 2) {
        unsigned constant = write_regs[WR12] | (write_regs[WR13] << 8);
        clocks *= 2 * (constant + 2);
    }
    // The Mac serial clock is 3.6864 MHz. Count half-bits to include 1.5
    // stop bits as well as the programmed data width and parity bit.
    static const unsigned data_bits[] = {5, 7, 6, 8};
    static const unsigned stop_half_bits[] = {0, 2, 3, 4};
    unsigned data = data_bits[(transmit ? write_regs[WR5] >> 5 : write_regs[WR3] >> 6) & 3];
    unsigned half_bits = 2 * (1 + data + (write_regs[WR4] & 1)) +
                         stop_half_bits[(write_regs[WR4] >> 2) & 3];
    return std::max(uint64_t(1), clocks * half_bits * 1000000000ULL / (2 * 3686400));
}

void EsccChannel::update_receive_timer()
{
    auto timers = TimerManager::get_instance();
    if (rx_timer) timers->cancel_timer(rx_timer);
    rx_timer = 0;
    if (write_regs[WR3] & WR3_RX_ENABLE)
        rx_timer = timers->add_cyclic_timer(character_period(), [this] { receive_tick(); });
}

void EsccChannel::receive_tick()
{
    if (chario->rcv_char_available_now()) {
        uint8_t byte = 0;
        if (chario->rcv_char(&byte) == 0) {
            if (rx_fifo.size() < 3) rx_fifo.push_back(byte);
            else read_regs[RR1] |= RR1_RX_OVERRUN_ERROR;
        }
    }
    poll_dma();
    if (interrupt_changed) interrupt_changed();
}

void EsccChannel::attach_backend(int id)
{
    switch(id) {
    case CHARIO_BE_NULL:
        this->chario = std::unique_ptr<CharIoBackEnd> (new CharIoNull);
        break;
    case CHARIO_BE_STDIO:
        this->chario = std::unique_ptr<CharIoBackEnd> (new CharIoStdin);
        break;
#ifdef _WIN32
#else
    case CHARIO_BE_SOCKET:
        this->chario = std::make_unique<CharIoSocket>(
            name == "ESCC_B" ? "chungussocket-b" : "chungussocket");
        break;
#endif
    default:
        LOG_F(ERROR, "%s: unknown backend ID %d, using NULL instead", this->name.c_str(), id);
        this->chario = std::unique_ptr<CharIoBackEnd> (new CharIoNull);
    }
}

void EsccChannel::reset(bool hw_reset)
{
    this->chario->rcv_disable();
    if (rx_timer) TimerManager::get_instance()->cancel_timer(rx_timer);
    if (tx_timer) TimerManager::get_instance()->cancel_timer(tx_timer);
    rx_timer = tx_timer = 0;
    rx_fifo.clear();
    this->tx_pending = false;
    this->ext_pending = false;
    this->first_rx_armed = true;

    /*
        We use hex values here instead of enums to more
        easily compare with the z85c30 data sheet.
    */

    this->write_regs[WR0] = 0;
    this->write_regs[WR1] &= 0x24;
    this->write_regs[WR3] &= 0xFE;
    this->write_regs[WR4] |= 0x04;
    this->write_regs[WR5] &= 0x61;
    this->write_regs[WR15] = 0xF8;

    this->read_regs[RR0] &= 0x38;
    this->read_regs[RR0] |= 0x44;
    this->read_regs[RR1]  = 0x06 | RR1_ALL_SENT; // HACK: also set ALL_SENT flag.
    this->read_regs[RR3]  = 0x00;
    this->read_regs[RR10] = 0x00;

    // initialize DPLL
    this->dpll_active    = 0;
    this->dpll_mode      = DpllMode::NRZI;
    this->dpll_clock_src = 0;

    // initialize Baud Rate Generator (BRG)
    this->brg_active    = 0;
    this->brg_clock_src = 0;

    if (hw_reset) {
        this->write_regs[WR9] &= 0x03; // clear all except (WR9_NO_VECTOR | WR9_VECTOR_INCLUDES_STATUS)
        this->write_regs[WR9] |= 0xC0; // set WR9_FORCE_HARDWARE_RESET
        this->write_regs[WR10] = 0;
        this->write_regs[WR11] = 8;
        this->write_regs[WR14] &= 0xC0;
    } else {
        this->write_regs[WR9] &= ~0x20; // clear WR9_INTERRUPT_MASKING_WITHOUT_INTACK
        this->write_regs[WR10] &= 0x60;
        this->write_regs[WR14] &= 0xC3;
    }
    this->write_regs[WR14] |= 0x20;
}

void EsccChannel::write_reg(int reg_num, uint8_t value)
{
    if ((reg_num == WR7) && (this->write_regs[WR15] & WR15_SDLC_HDLC_ENHANCEMENT_ENABLE))
        reg_num = WR7Prime;

    switch (reg_num) {
    case WR1:
        if ((value & WR1_TX_INT_ENABLE) && !(write_regs[WR1] & WR1_TX_INT_ENABLE) &&
            (read_regs[RR0] & RR0_TX_BUFFER_EMPTY))
            tx_pending = true;
        if ((value & WR1_RECEIVE_INTERRUPT_MODES) !=
                (write_regs[WR1] & WR1_RECEIVE_INTERRUPT_MODES))
            first_rx_armed = true;
        break;
    case WR3:
        if ((this->write_regs[WR3] ^ value) & WR3_ENTER_HUNT_MODE) {
            this->write_regs[WR3] |= WR3_ENTER_HUNT_MODE;
            this->read_regs[RR0] |= RR0_SYNC_HUNT;
            LOG_F(9, "%s: Hunt mode entered.", this->name.c_str());
        }
        if ((this->write_regs[WR3] ^ value) & WR3_RX_ENABLE) {
            if (value & WR3_RX_ENABLE) {
                this->write_regs[WR3] |= WR3_RX_ENABLE;
                this->chario->rcv_enable();
                LOG_F(9, "%s: receiver enabled.", this->name.c_str());
            } else {
                this->write_regs[WR3] ^= WR3_RX_ENABLE;
                this->chario->rcv_disable();
                LOG_F(9, "%s: receiver disabled.", this->name.c_str());
                this->write_regs[WR3] |= WR3_ENTER_HUNT_MODE;
                this->read_regs[RR0] |= RR0_SYNC_HUNT;
            }
        }
        this->write_regs[WR3] =
            (this->write_regs[WR3] & (WR3_RX_ENABLE | WR3_ENTER_HUNT_MODE)) |
            (value & ~(WR3_RX_ENABLE | WR3_ENTER_HUNT_MODE));
        update_receive_timer();
        return;
    case WR4:
        if ((value & WR4_STOP_BITS) == WR4_SYNC_MODES_ENABLE &&
            (value & WR4_SYNC_MODE) == WR4_SDLC_MODE &&
             !(this->write_regs[WR3] & WR3_RX_ENABLE))
             this->read_regs[RR0] |= RR0_SYNC_HUNT;
        break;
    case WR8:
        this->send_byte(value);
        return;
    case WR14:
        switch (value & WR14_DPLL_COMMAND_BITS) {
        case WR14_DPLL_NULL_COMMAND:
            break;
        case WR14_DPLL_ENTER_SEARCH_MODE:
            this->dpll_active = 1;
            this->read_regs[RR10] &= ~(RR10_TWO_CLOCKS_MISSING | RR10_ONE_CLOCK_MISSING);
            break;
        case WR14_DPLL_RESET_MISSING_CLOCK:
            this->read_regs[RR10] &= ~(RR10_TWO_CLOCKS_MISSING | RR10_ONE_CLOCK_MISSING);
            break;
        case WR14_DPLL_DISABLE_DPLL:
            this->dpll_active = 0;
            // fallthrough
        case WR14_DPLL_SET_SOURCE_BR_GENERATOR:
            this->dpll_clock_src = 0;
            break;
        case WR14_DPLL_SET_SOURCE_RTXC:
            this->dpll_clock_src = 1;
            break;
        case WR14_DPLL_SET_FM_MODE:
            this->dpll_mode = DpllMode::FM;
            break;
        case WR14_DPLL_SET_NRZI_MODE:
            this->dpll_mode = DpllMode::NRZI;
            break;
        }
        if (value & (WR14_LOCAL_LOOPBACK | WR14_AUTO_ECHO | WR14_DTR_REQUEST_FUNCTION)) {
            LOG_F(WARNING, "%s: unexpected value in WR14 = 0x%X", this->name.c_str(), value);
        }
        if (this->brg_clock_src ^ (value & WR14_BR_GENERATOR_SOURCE)) {
            this->brg_clock_src = value & WR14_BR_GENERATOR_SOURCE;
        }
        if (this->brg_active ^ (value & WR14_BR_GENERATOR_ENABLE)) {
            this->brg_active = value & WR14_BR_GENERATOR_ENABLE;
            LOG_F(9, "%s: BRG %s", this->name.c_str(), this->brg_active ? "enabled" : "disabled");
        }
        break;
    }

    this->write_regs[reg_num] = value;
    if (reg_num == WR4 || reg_num == WR11 || reg_num == WR12 || reg_num == WR13 || reg_num == WR14)
        update_receive_timer();
}

uint8_t EsccChannel::read_reg(int reg_num)
{
    switch (reg_num) {
    case RR0:
        update_modem_status();
        if ((write_regs[WR3] & WR3_RX_ENABLE) && !rx_fifo.empty()) {
            return this->read_regs[RR0] |= RR0_RX_CHARACTER_AVAILABLE;
        } else {
            return this->read_regs[RR0] &= ~RR0_RX_CHARACTER_AVAILABLE;
        }
        break;
    case RR8:
        return this->receive_byte();
    }
    return this->read_regs[reg_num];
}

void EsccChannel::send_byte(uint8_t value)
{
    // TODO: put one byte into the Data FIFO

    this->write_regs[WR8] = value;
    this->chario->xmit_char(value);
    tx_pending = false;
    read_regs[RR0] &= ~RR0_TX_BUFFER_EMPTY;
    read_regs[RR1] &= ~RR1_ALL_SENT;
    if (tx_timer) TimerManager::get_instance()->cancel_timer(tx_timer);
    tx_timer = TimerManager::get_instance()->add_oneshot_timer(character_period(true), [this] {
        tx_timer = 0;
        read_regs[RR0] |= RR0_TX_BUFFER_EMPTY;
        read_regs[RR1] |= RR1_ALL_SENT;
        tx_pending = true;
        poll_dma();
        if (interrupt_changed) interrupt_changed();
    });
}

uint8_t EsccChannel::receive_byte()
{
    uint8_t c;

    if (!rx_fifo.empty()) {
        c = rx_fifo.front();
        rx_fifo.pop_front();
    } else {
        c = 0;
    }
    this->read_regs[RR0] &= ~RR0_RX_CHARACTER_AVAILABLE;
    this->read_regs[RR8] = c;
    this->first_rx_armed = false;
    return c;
}

uint8_t EsccChannel::interrupt_pending()
{
    uint8_t pending = 0;
    if (ext_pending && (write_regs[WR1] & WR1_EXT_INT_ENABLE))
        pending |= RR3_CHANNEL_B_EXT_STAT_IP;
    if (tx_pending && (write_regs[WR1] & WR1_TX_INT_ENABLE) &&
        (write_regs[WR5] & WR5_TX_ENABLE))
        pending |= RR3_CHANNEL_B_TX_IP;
    unsigned mode = write_regs[WR1] & WR1_RECEIVE_INTERRUPT_MODES;
    bool special = read_regs[RR1] & (RR1_PARITY_ERROR | RR1_RX_OVERRUN_ERROR | RR1_CRC_FRAMING_ERROR);
    if ((write_regs[WR3] & WR3_RX_ENABLE) &&
        ((special && mode != WR1_RX_INT_DISABLE) ||
         (!rx_fifo.empty() &&
          (mode == WR1_INT_ON_ALL_RX_CHARACTERS_OR_SPECIAL_CONDITION ||
           (mode == WR1_RX_INT_ON_FIRST_CHARACTER_OR_SPECIAL_CONDITION && first_rx_armed)))))
        pending |= RR3_CHANNEL_B_RX_IP;
    return pending;
}

void EsccChannel::command(uint8_t value)
{
    switch (value & WR0_COMMAND_CODES) {
    case WR0_COMMAND_RESET_TXINT_PENDING: tx_pending = false; break;
    case WR0_COMMAND_RESET_EXT_STATUS_INTERRUPTS: ext_pending = false; break;
    case WR0_COMMAND_ENABLE_INT_ON_NEXT_RX_CHARACTER: first_rx_armed = true; break;
    case WR0_COMMAND_ERROR_RESET:
        read_regs[RR1] &= ~(RR1_PARITY_ERROR | RR1_RX_OVERRUN_ERROR | RR1_CRC_FRAMING_ERROR);
        break;
    }
}

void EsccChannel::update_modem_status()
{
    constexpr uint8_t mask = RR0_DCD | RR0_CTS;
    uint8_t lines = chario->connected() ? mask : 0;
    if ((read_regs[RR0] ^ lines) & write_regs[WR15] & mask)
        ext_pending = true;
    read_regs[RR0] = (read_regs[RR0] & ~mask) | lines;
}

void EsccChannel::poll_dma()
{
    // Accept connections and reap closed peers even while the guest receiver
    // is disabled; otherwise an unused port can fill its listen backlog.
    chario->rcv_char_available_now();
    update_modem_status();
    if (dma_channels[DIR_TX] && (write_regs[WR5] & WR5_TX_ENABLE) &&
        (read_regs[RR0] & RR0_TX_BUFFER_EMPTY))
        dma_channels[DIR_TX]->xfer_retry();
    if (dma_channels[DIR_RX] && (write_regs[WR3] & WR3_RX_ENABLE) &&
        !rx_fifo.empty())
        dma_channels[DIR_RX]->xfer_retry();
}

uint8_t EsccChannel::get_enh_reg()
{
    return this->enh_reg;
}

void EsccChannel::set_enh_reg(uint8_t value)
{
    uint8_t changed_bits = value ^ this->enh_reg;
    if (changed_bits & 0x10) {
        if (value & 0x10)
            LOG_F(ERROR, "%s: CTS connected to GPIO; DCD connected to GND", this->name.c_str());
        else
            LOG_F(INFO, "%s: CTS connected to TRXC_In_l; DCD connected to GPIO", this->name.c_str());
        this->enh_reg = value & 0x10;
    } else if (changed_bits & ~0x10) {
        if (value & ~0x10)
            LOG_F(ERROR, "%s: Ignoring attempt to set Enh_Reg bits 0x%02x", this->name.c_str(), value & ~0x10);
    }
}

int EsccChannel::xfer_from(DmaChannel *ch_obj, uint8_t *buf, int len) {
    if (ch_obj->get_id() == DIR_TX) {
        LOG_F(WARNING, "%s: attempt to receive data over the DMA TX channel",
              this->name.c_str());
        return 0;
    }

    int bytes_moved = 0;

    while (len > 0 && !rx_fifo.empty()) {
        *buf++ = this->receive_byte();
        len--;
        bytes_moved++;
    }

    return bytes_moved;
}

int EsccChannel::xfer_to(DmaChannel *ch_obj, uint8_t *buf, int len) {
    if (ch_obj->get_id() == DIR_RX) {
        LOG_F(WARNING, "%s: attempt to transmit data over the DMA RX channel",
              this->name.c_str());
        return 0;
    }

    if (len <= 0 || !(write_regs[WR5] & WR5_TX_ENABLE) ||
        !(read_regs[RR0] & RR0_TX_BUFFER_EMPTY)) return 0;
    send_byte(*buf);
    return 1;
}

static const std::vector<std::string> CharIoBackends = {"null", "stdio", "socket"};

static const PropMap Escc_Properties = {
    {"serial_backend", new StrProperty("null", CharIoBackends)},
    {"serial_b_backend", new StrProperty("null", CharIoBackends)},
};

static const DeviceDescription Escc_Descriptor = {
    EsccController::create, {}, Escc_Properties, HWCompType::MMIO_DEV
};

REGISTER_DEVICE(Escc, Escc_Descriptor);
