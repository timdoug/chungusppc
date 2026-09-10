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

/** @file Media Access Controller for Ethernet (MACE) definitions. */

#ifndef MACE_H
#define MACE_H

#include <devices/common/dmacore.h>
#include <devices/common/hwcomponent.h>
#include <devices/common/hwinterrupt.h>
#include <devices/ethernet/enetbackend.h>

#include <cinttypes>
#include <deque>
#include <memory>
#include <vector>

/** Known MACE chip IDs. */
constexpr auto MACE_ID_REV_B0 = 0x0940;    // Darwin-0.3 source
constexpr auto MACE_ID_REV_A2 = 0x0941;    // Darwin-0.3 source & Curio datasheet

/** MACE registers offsets. */
// Refer to the Am79C950 datasheet for details
namespace MaceEnet {
    enum MaceReg : uint8_t {
        Rcv_FIFO        =  0,
        Xmit_FIFO       =  1,
        Xmit_Frame_Ctrl =  2,
        Xmit_Frame_Stat =  3,
        Xmit_Retry_Cnt  =  4,
        Rcv_Frame_Ctrl  =  5,
        Rcv_Frame_Stat  =  6,
        FIFO_Frame_Cnt  =  7,
        Interrupt       =  8,
        Interrupt_Mask  =  9,
        Poll            = 10,
        BIU_Config_Ctrl = 11,
        FIFO_Config     = 12,
        MAC_Config_Ctrl = 13,
        PLS_Config_Ctrl = 14,
        PHY_Config_Ctrl = 15,
        Chip_ID_Lo      = 16,
        Chip_ID_Hi      = 17,
        Int_Addr_Config = 18,
        Log_Addr_Flt    = 20,
        Phys_Addr       = 21,
        Missed_Pkt_Cnt  = 24,
        Runt_Pkt_Cnt    = 26, // not used in Macintosh?
        Rcv_Collis_Cnt  = 27, // not used in Macintosh?
        User_Test       = 29,
        Rsrvd_Test_1    = 30, // not used in Macintosh?
        Rsrvd_Test_2    = 31, // not used in Macintosh?
    };

    /** Bit definitions for BIU_Config_Ctrl register. */
    enum {
        BIU_SWRST   = 1 << 0,
    };

    /** Bit definitions for the internal configuration register. */
    enum {
        IAC_LOGADDR = 1 << 1,
        IAC_PHYADDR = 1 << 2,
        IAC_ADDRCHG = 1 << 7
    };

    /** Bit definitions for the Interrupt and Interrupt_Mask registers. */
    enum {
        IR_XMTINT   = 1 << 0, // frame transmitted
        IR_RCVINT   = 1 << 1, // frame received
        IR_MPCO     = 1 << 2, // missed packet count overflow
        IR_RNTPCO   = 1 << 3, // runt packet count overflow
        IR_RCVCCO   = 1 << 4, // receive collision count overflow
        IR_CERR     = 1 << 5, // collision error
        IR_BABL     = 1 << 6, // babble error
        IR_JAB      = 1 << 7, // jabber error
    };

    /** Bit definitions for MAC_Config_Ctrl register. */
    enum {
        MACCC_ENRCV    = 1 << 0, // enable receiver
        MACCC_ENXMT    = 1 << 1, // enable transmitter
        MACCC_DRCVBC   = 1 << 2, // disable receive broadcast
        MACCC_DRCVPA   = 1 << 3, // disable receive physical address
        MACCC_EMBA     = 1 << 5, // enable modified backoff algorithm
        MACCC_DXMT2PD  = 1 << 6, // disable transmit two part deferral
        MACCC_PROM     = 1 << 7, // promiscuous mode
    };

    /** Bit definitions for Xmit_Frame_Stat register. */
    enum {
        XMTFS_XMTSV = 1 << 7, // transmit status valid
    };

    /** Number of status bytes the MACE appends to each received frame. */
    constexpr int RCV_STATUS_LEN = 4;

} // namespace MaceEnet

/** Device status bit the receive DMA channel raises at the end of a packet.
    Drivers test it to tell a completed frame from a still-filling buffer. */
constexpr uint8_t MACE_DBDMA_EOP = 0x40; // CH_STAT_S6

/** How often the host side is given a chance to deliver frames. */
constexpr int MACE_POLL_INTERVAL_US = 1000;

/** Cap on frames held for the guest before they start being counted as missed. */
constexpr size_t MACE_MAX_RCV_QUEUE = 64;


class MaceController : public DmaDevice, public HWComponent {
public:
    MaceController(uint16_t id) {
        this->chip_id = id;
        this->set_name("MACE");
        this->supports_types(HWCompType::MMIO_DEV | HWCompType::ETHER_MAC);
    }
    ~MaceController() = default;

    static std::unique_ptr<HWComponent> create() {
        return std::unique_ptr<MaceController>(new MaceController(MACE_ID_REV_A2));
    }

    // MACE registers access
    uint8_t read(uint8_t reg_offset);
    void    write(uint8_t reg_offset, uint8_t value);

    /** Attach the host side and the interrupt controller this MAC reports to. */
    void set_backend(std::unique_ptr<EthernetBackend> backend);
    /** Program the station address the chip powers up with. On real HW the
        boot firmware copies it out of the Ethernet address PROM; drivers then
        read it back from Phys_Addr rather than reading the PROM themselves. */
    void set_mac_address(const uint8_t addr[6]) {
        this->phys_addr = 0;
        for (int i = 0; i < 6; i++)
            this->phys_addr |= (uint64_t)addr[i] << (i * 8);
    }

    void register_enet_int(InterruptCtrl *int_ctrl_obj, uint64_t irq_id) {
        this->int_ctrl = int_ctrl_obj;
        this->irq_id   = irq_id;
    }

    int device_postinit() override;

    int xfer_to(DmaChannel *ch_obj, uint8_t *buf, int len) override;
    int xfer_from(DmaChannel *ch_obj, uint8_t *buf, int len) override;

private:
    void update_irq();
    void set_int_flags(uint8_t flags);
    void receive_frame(const uint8_t *frame, int len);
    bool accept_frame(const uint8_t *frame, int len);
    void poll_backend();

    std::unique_ptr<EthernetBackend> backend = nullptr;
    InterruptCtrl*  int_ctrl = nullptr;
    uint64_t        irq_id   = 0;
    bool            irq_line = false;

    // frame being assembled from the transmit DMA channel
    uint8_t     xmt_frame[ENET_MAX_FRAME_SIZE];
    int         xmt_frame_len = 0;

    // frames accepted from the backend, and the one being handed to the
    // receive DMA channel. Each carries RCV_STATUS_LEN status bytes after the
    // frame, the way the receive FIFO delivers them.
    std::deque<std::vector<uint8_t>> rcv_queue;
    std::vector<uint8_t>             rcv_frame;
    size_t                           rcv_pos       = 0;
    uint32_t                         poll_timer_id = 0;

    uint16_t    chip_id;          // per-instance MACE Chip ID
    uint8_t     addr_cfg      = 0;
    uint8_t     addr_ptr      = 0;
    uint8_t     xmt_fs        = 0;
    uint8_t     xmt_retry     = 0;
    uint8_t     rcv_fc        = 1;
    uint8_t     rcv_fs        = 0;
    uint8_t     biu_ctrl      = 0;
    uint8_t     fifo_ctrl     = 0;

    uint8_t     poll_reg      = 0;

    uint8_t     mac_cc        = 0;
    uint8_t     pls_cc        = 0;
    uint8_t     phy_cc        = 0;
    uint8_t     fifo_fc       = 0;
    uint8_t     missed_pkts   = 0;
    uint64_t    phys_addr     = 0;
    uint64_t    log_addr      = 0;

    // interrupt stuff
    uint8_t     int_stat  = 0;
    uint8_t     int_mask  = 0xFF; // all interrupts masked after reset
};

#endif // MACE_H
