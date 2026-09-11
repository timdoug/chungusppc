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

/** @file Media Access Controller for Ethernet (MACE) emulation. */

#include <core/timermanager.h>
#include <devices/deviceregistry.h>
#include <devices/ethernet/mace.h>
#include <loguru.hpp>

#include <cinttypes>
#include <algorithm>
#include <cstring>

using namespace MaceEnet;

MaceController::~MaceController() {
    if (this->poll_timer_id)
        TimerManager::get_instance()->cancel_timer(this->poll_timer_id);
    if (this->backend)
        this->backend->stop();
}

uint8_t MaceController::read(uint8_t reg_offset)
{
    switch (reg_offset) {
    case MaceReg::Rcv_FIFO:
        return 0; //No FIFO yet
    case MaceReg::Rcv_Frame_Ctrl:
        return this->rcv_fc;
    case MaceReg::Xmit_Frame_Ctrl:
        return this->xmt_fc;
    case MaceReg::User_Test:
        return this->user_test;
    case MaceReg::Xmit_Frame_Stat:
        return this->xmt_fs;
    case MaceReg::Xmit_Retry_Cnt:
        return this->xmt_retry;
    case MaceReg::Rcv_Frame_Stat:
        return this->rcv_fs;
    case MaceReg::FIFO_Frame_Cnt:
        return this->fifo_fc;
    case MaceReg::Interrupt: {
        uint8_t ret_val = this->int_stat;
        this->int_stat = 0;
        this->update_irq(); // clearing the flags releases the interrupt line
        LOG_F(9, "%s: all interrupt flags cleared", this->name.c_str());
        return ret_val;
    }
    case MaceReg::Interrupt_Mask:
        return this->int_mask;
    case MaceReg::Poll:
        return this->poll_reg;
    case MaceReg::BIU_Config_Ctrl:
        return this->biu_ctrl;
    case MaceReg::FIFO_Config:
        return this->fifo_ctrl;
    case MaceReg::MAC_Config_Ctrl:
        return this->mac_cc;
    case MaceReg::PLS_Config_Ctrl:
        return this->pls_cc;
    case MaceReg::PHY_Config_Ctrl:
        return this->phy_cc;
    case MaceReg::Chip_ID_Lo:
        return this->chip_id & 0xFFU;
    case MaceReg::Chip_ID_Hi:
        return (this->chip_id >> 8) & 0xFFU;
    case MaceReg::Int_Addr_Config:
        return this->addr_cfg;
    case MaceReg::Phys_Addr:
        // Drivers arm IAC_PHYADDR and then read this register once per address
        // byte to learn the station address, so it auto-increments the same way
        // writing it does.
        if (this->addr_cfg & IAC_PHYADDR) {
            uint8_t val = (this->phys_addr >> (this->addr_ptr * 8)) & 0xFFU;
            if (++this->addr_ptr >= 6) {
                this->addr_cfg &= ~IAC_PHYADDR;
                this->addr_ptr = 0;
            }
            return val;
        }
        return 0;
    case MaceReg::Log_Addr_Flt:
        if (this->addr_cfg & IAC_LOGADDR) {
            uint8_t val = (this->log_addr >> (this->addr_ptr * 8)) & 0xFFU;
            if (++this->addr_ptr >= 8) {
                this->addr_cfg &= ~IAC_LOGADDR;
                this->addr_ptr = 0;
            }
            return val;
        }
        return 0;
    case MaceReg::Missed_Pkt_Cnt:
        return this->missed_pkts;
    default:
        LOG_F(INFO, "%s: reading from register %d", this->name.c_str(), reg_offset);
    }

    return 0;
}

void MaceController::write(uint8_t reg_offset, uint8_t value)
{
    switch(reg_offset) {
    case MaceReg::Rcv_Frame_Ctrl:
        this->rcv_fc = value;
        break;
    case MaceReg::Xmit_Frame_Ctrl:
        this->xmt_fc = value;
        break;
    case MaceReg::User_Test:
        this->user_test = value;
        break;
    case MaceReg::Interrupt_Mask:
        this->int_mask = value;
        this->update_irq();
        break;
    case MaceReg::BIU_Config_Ctrl:
        if (value & BIU_SWRST) {
            LOG_F(INFO, "%s: soft reset asserted", this->name.c_str());
            value &= ~BIU_SWRST; // acknowledge soft reset
            this->int_stat      = 0;
            this->int_mask      = 0xFF; // reset masks every interrupt again
            this->xmt_frame_len = 0;
            this->xmt_fs = 0;
            this->rcv_queue.clear();
            this->rcv_frame.clear();
            this->rcv_pos = 0;
            this->update_irq();
        }
        this->biu_ctrl = value;
        break;
    case MaceReg::FIFO_Config:
        this->fifo_ctrl = value;
        break;
    case MaceReg::MAC_Config_Ctrl:
        this->mac_cc = value;
        break;
    case MaceReg::PLS_Config_Ctrl:
        this->pls_cc = value;
        if (value != 0 && value != 7) // PDM uses AUI; PCI machines use GPSI.
            LOG_F(WARNING, "%s: unsupported transceiver interface 0x%X in PLSCC",
                  this->name.c_str(), value);
        break;
    case MaceReg::PHY_Config_Ctrl:
        this->phy_cc = value;
        break;
    case MaceReg::Int_Addr_Config:
        if ((value & IAC_LOGADDR) && (value & IAC_PHYADDR))
            value &= ~IAC_PHYADDR;
        if (value & (IAC_LOGADDR | IAC_PHYADDR))
            this->addr_ptr = 0;
        // ADDRCHG asks the chip to let the address registers be rewritten and
        // clears itself once they are ready. Nothing here takes any time, so
        // drop it at once: drivers spin until it reads back as zero.
        this->addr_cfg = value & ~IAC_ADDRCHG;
        break;
    case MaceReg::Log_Addr_Flt:
        if (this->addr_cfg & IAC_LOGADDR) {
            uint64_t mask = ~(0xFFULL << (this->addr_ptr * 8));
            this->log_addr = (this->log_addr & mask) | ((uint64_t)value << (this->addr_ptr * 8));
            if (++this->addr_ptr >= 8) {
                this->addr_cfg &= ~IAC_LOGADDR;
                this->addr_ptr = 0;
            }
        }
        break;
    case MaceReg::Phys_Addr:
        if (this->addr_cfg & IAC_PHYADDR) {
            uint64_t mask = ~(0xFFULL << (this->addr_ptr * 8));
            this->phys_addr = (this->phys_addr & mask) | ((uint64_t)value << (this->addr_ptr * 8));
            if (++this->addr_ptr >= 6) {
                this->addr_cfg &= ~IAC_PHYADDR;
                this->addr_ptr = 0;
            }
        }
        break;
    default:
        LOG_F(INFO, "%s: writing 0x%X to register %d", this->name.c_str(),
              value, reg_offset);
    }
}

void MaceController::set_backend(std::unique_ptr<EthernetBackend> new_backend) {
    if (this->backend)
        this->backend->stop();

    this->backend = std::move(new_backend);

    if (!this->backend)
        return;

    uint8_t mac_addr[6];
    for (int i = 0; i < 6; i++)
        mac_addr[i] = (this->phys_addr >> (i * 8)) & 0xFFU;
    this->backend->start(mac_addr);

    this->backend->set_rcv_callback([this](const uint8_t *frame, int len) {
        this->receive_frame(frame, len);
    });
}

int MaceController::device_postinit() {
    // Timers can't be armed while the machine is still being built, so start
    // polling the host side once everything is up.
    this->poll_backend();
    return 0;
}

// Backends aren't allowed to hand us frames from another thread, so give them
// a regular chance to deliver what they have.
void MaceController::poll_backend() {
    if (this->backend)
        this->backend->poll();

    this->poll_timer_id = TimerManager::get_instance()->add_oneshot_timer(
        USECS_TO_NSECS(MACE_POLL_INTERVAL_US),
        [this]() {
            this->poll_timer_id = 0;
            this->poll_backend();
        });
}

// Decide whether a frame off the wire is ours, following MAC_Config_Ctrl.
bool MaceController::accept_frame(const uint8_t *frame, int len) {
    if (len < 6)
        return false;

    if (this->mac_cc & MaceEnet::MACCC_PROM)
        return true; // promiscuous: everything is ours

    bool is_broadcast = true;
    for (int i = 0; i < 6; i++)
        if (frame[i] != 0xFF) { is_broadcast = false; break; }

    if (is_broadcast)
        return !(this->mac_cc & MaceEnet::MACCC_DRCVBC);

    if (frame[0] & 1) {
        // Multicast. The logical address filter is a 64 bit hash over the CRC
        // of the destination address; accept everything until it's implemented
        // so multicast protocols keep working.
        return true;
    }

    bool is_ours = true;
    for (int i = 0; i < 6; i++)
        if (frame[i] != ((this->phys_addr >> (i * 8)) & 0xFFU)) { is_ours = false; break; }

    return is_ours && !(this->mac_cc & MaceEnet::MACCC_DRCVPA);
}

void MaceController::receive_frame(const uint8_t *frame, int len) {
    if (len <= 0 || len > ENET_MAX_FRAME_SIZE)
        return;

    if (!(this->mac_cc & MaceEnet::MACCC_ENRCV) || !this->accept_frame(frame, len))
        return;

    if (this->packet_dma_receive) {
        if (this->packet_dma_receive(frame, len))
            this->set_int_flags(MaceEnet::IR_RCVINT);
        return;
    }

    if (this->rcv_queue.size() >= MACE_MAX_RCV_QUEUE) {
        // No room left: count it the way the chip does rather than grow forever.
        if (this->missed_pkts != 0xFF)
            this->missed_pkts++;
        return;
    }

    // The receive FIFO hands the frame over followed by its status bytes, and
    // drivers size their buffers for both.
    std::vector<uint8_t> buf(frame, frame + len);
    buf.push_back(len & 0xFF);
    buf.push_back((len >> 8) & 0x0F);
    buf.push_back(0); // runt packet count
    buf.push_back(0); // receive collision count
    this->rcv_queue.push_back(std::move(buf));

    // Wake the receive channel: it is parked on a descriptor waiting for us.
    if (this->channel_obj)
        this->channel_obj->xfer_retry();
}

int MaceController::xfer_from(DmaChannel *ch_obj, uint8_t *buf, int len) {
    if (this->rcv_pos >= this->rcv_frame.size()) {
        if (this->rcv_queue.empty())
            return 0; // nothing to receive: leave the transfer pending

        this->rcv_frame = std::move(this->rcv_queue.front());
        this->rcv_queue.pop_front();
        this->rcv_pos = 0;
    }

    size_t remaining = this->rcv_frame.size() - this->rcv_pos;
    size_t moved     = std::min(remaining, (size_t)std::max(len, 0));

    std::memcpy(buf, &this->rcv_frame[this->rcv_pos], moved);
    this->rcv_pos += moved;

    if (this->rcv_pos >= this->rcv_frame.size()) {
        // End of packet. Drivers size each descriptor for a full frame, so this
        // one has to finish early with the unused remainder left in resCount.
        ch_obj->end_xfer(MACE_DBDMA_EOP);
        this->rcv_fs = 0;
        this->set_int_flags(MaceEnet::IR_RCVINT);
    }

    return (int)moved;
}

// The MACE interrupt is level triggered: it stays asserted while any unmasked
// flag is set, and reading the Interrupt register clears the flags.
void MaceController::update_irq() {
    if (!this->int_ctrl)
        return;

    bool new_line = (this->int_stat & ~this->int_mask) != 0;
    if (new_line != this->irq_line) {
        this->irq_line = new_line;
        this->int_ctrl->ack_int(this->irq_id, new_line);
    }
}

void MaceController::set_int_flags(uint8_t flags) {
    this->int_stat |= flags;
    this->update_irq();
}

int MaceController::xfer_to(DmaChannel *ch_obj, uint8_t *buf, int len) {
    if (len <= 0)
        return 0;

    // Drop what won't fit rather than truncating into the next frame, so a
    // runaway length can't splice two frames together.
    if (this->xmt_frame_len + len > ENET_MAX_FRAME_SIZE) {
        LOG_F(WARNING, "%s: frame longer than %d bytes, dropping",
              this->name.c_str(), ENET_MAX_FRAME_SIZE);
        this->xmt_frame_len = -1;
    }

    if (this->xmt_frame_len >= 0) {
        std::memcpy(&this->xmt_frame[this->xmt_frame_len], buf, len);
        this->xmt_frame_len += len;
    }

    // The transmit list ends each frame with an OUTPUT_LAST command, so that
    // is where the frame is complete and goes out.
    if (ch_obj->is_last_xfer()) {
        this->transmit_frame(this->xmt_frame, this->xmt_frame_len);
        this->xmt_frame_len = 0;
    }

    return len;
}

bool MaceController::transmit_frame(const uint8_t *frame, int len) {
    if (!(this->mac_cc & MaceEnet::MACCC_ENXMT) || len <= 0 || len > ENET_MAX_FRAME_SIZE)
        return false;
    if (this->backend)
        this->backend->send_frame(frame, len);
    this->xmt_fs = MaceEnet::XMTFS_XMTSV;
    this->set_int_flags(MaceEnet::IR_XMTINT);
    return true;
}

static const PropMap Mace_properties = {
    {"enet_backend", new StrProperty("null",
        std::vector<std::string>({"null", "loopback", "slirp"}))},
    {"enet_hostfwd", new StrProperty("")},
};

static const DeviceDescription Mace_Descriptor = {
    MaceController::create, {}, Mace_properties,
    HWCompType::MMIO_DEV | HWCompType::ETHER_MAC
};

REGISTER_DEVICE(Mace, Mace_Descriptor);
