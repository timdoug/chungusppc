// Exercise the AMIC register sequences used by Mac OS and MkLinux.
#include <core/timermanager.h>
#include <cpu/ppc/ppcemu.h>
#include <devices/common/scsi/sc53c94.h>
#include <devices/floppy/swim3.h>
#include <devices/ioctrl/amic.h>
#include <devices/ethernet/mace.h>
#include <devices/memctrl/memctrlbase.h>
#include <machines/machinebase.h>
#include <machines/machineproperties.h>
#include <loguru.hpp>
#include <cstdio>
#include <cstring>
#include <vector>

struct WireProbe : EthernetBackend {
    std::vector<std::vector<uint8_t>> sent;
    bool start(const uint8_t[6]) override { return true; }
    void stop() override {}
    void send_frame(const uint8_t *frame, int len) override {
        sent.emplace_back(frame, frame + len);
    }
    void inject(const std::vector<uint8_t> &frame) {
        deliver_frame(frame.data(), frame.size());
    }
};

struct PacketChannel : DmaChannel {
    bool last = false, ready = false;
    uint8_t status = 0;
    bool is_last_xfer() override { return last; }
    void xfer_retry() override { ready = true; }
    void end_xfer(uint8_t value) override { status = value; }
};

int main() {
    loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
    auto tm = TimerManager::get_instance();
    tm->set_time_now_cb([]() { return uint64_t(0); });
    tm->set_notify_changes_cb([]() {});
    gMachineSettings["mon_id"] = std::make_unique<StrProperty>("VGA-SVGA");
    gMachineSettings["enet_backend"] = std::make_unique<StrProperty>("null");
    gMachineObj = std::make_unique<MachineBase>("AMIC regression test");
    gMachineObj->add_device("Sc53C94", std::make_unique<Sc53C94>());
    gMachineObj->add_device("Swim3", std::make_unique<Swim3::Swim3Ctrl>());
    gMachineObj->add_device("Mace", MaceController::create());
    MemCtrlBase memory;
    auto ram = memory.add_ram_region(0, 0x80000)->mem_ptr;
    mem_ctrl_instance = &memory;
    ppc_state.msr = 0; // Inspect the IRQ pin without entering a guest exception.
    int failures = 0, checks = 0;
    auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", name);
            ++failures;
        }
    };
    {
        AMIC amic;
        auto write = [&](uint32_t reg, uint8_t value) { amic.write(0, reg, value, 1); };
        auto read = [&](uint32_t reg) { return amic.read(0, reg, 1); };
        auto irq = [&](IntSrc source, bool state) {
            amic.ack_int(amic.register_dev_int(source), state);
        };
        write(AMICReg::Int_Ctrl, CPU_INT_MODE);
        irq(IntSrc::ESCC, true);
        check(int_pin && read(AMICReg::Int_Ctrl) == 0xC4, "68k rising edge latches");
        write(AMICReg::Int_Ctrl, 0xC0);
        check(!int_pin && read(AMICReg::Int_Ctrl) == 0x44, "68k ACK clears the latch");
        irq(IntSrc::ESCC, false);
        check(int_pin, "68k falling edge also latches");
        write(AMICReg::Int_Ctrl, 0xC0);

        irq(IntSrc::SCSI_CURIO, true);
        check(!int_pin, "masked SCSI request does not reach the CPU");
        write(AMICReg::VIA2_IER, 0x88);
        check(int_pin, "unmasking pending SCSI asserts the cascade");
        // Mach requests native mode at startup and ACKs after each handler.
        write(AMICReg::Int_Ctrl, 0x80);
        check(int_pin && read(AMICReg::Int_Ctrl) == 0x82, "native mode replaces 68k mode");
        write(AMICReg::Int_Ctrl, 0x80);
        check(int_pin, "native ACK preserves an unserviced request");
        irq(IntSrc::ETHERNET, true);
        irq(IntSrc::SCSI_CURIO, false);
        check(int_pin && read(AMICReg::Int_Ctrl) == 0x88, "another source keeps native IRQ asserted");
        irq(IntSrc::ETHERNET, false);
        check(!int_pin && read(AMICReg::Int_Ctrl) == 0, "last source releases native IRQ");
        irq(IntSrc::SCSI_CURIO, true);
        write(AMICReg::VIA2_IER, 0x08);
        check(!int_pin, "masking a pending cascade releases native IRQ");
        write(AMICReg::VIA2_IER, 0x88);
        check(int_pin, "unmasking reasserts the pending native IRQ");
        irq(IntSrc::SCSI_CURIO, false);

        // MkLinux writes the codec command high byte first, then polls bit 7.
        write(AMICReg::Snd_Ctrl_0, 0);
        write(AMICReg::Snd_Ctrl_1, 0);
        write(AMICReg::Snd_Ctrl_2, 0);
        write(AMICReg::Snd_Ctrl_0, 0xC0);
        write(AMICReg::Snd_Ctrl_1, 0x02);
        write(AMICReg::Snd_Ctrl_2, 0xBB);
        check(!(read(AMICReg::Snd_Ctrl_0) & 0x80), "completed codec command is not busy");
        check(read(AMICReg::Snd_Ctrl_1) == 0x02 && read(AMICReg::Snd_Ctrl_2) == 0xBB,
              "codec command payload remains readable");

        auto mace = dynamic_cast<MaceController*>(gMachineObj->get_comp_by_name("Mace"));
        auto backend = std::make_unique<WireProbe>();
        auto wire = backend.get();
        mace->set_backend(std::move(backend));
        uint8_t station[6] = {8, 0, 7, 0x61, 0, 1};
        unsigned checksum = 0;
        for (unsigned i = 0; i < 8; ++i) {
            unsigned raw = read(0x8001 + 16 * i), byte = 0;
            for (unsigned bit = 0; bit < 8; ++bit)
                byte |= ((raw >> bit) & 1) << (7 - bit);
            checksum ^= byte;
            if (i < 6)
                check(byte == station[i], "PROM supplies the station address with reversed bits");
            check(read(0x9001 + 16 * i) == raw, "PROM mirror agrees");
        }
        check(checksum == 0xFF, "PROM checksum covers all eight bytes");
        mace->write(MaceEnet::Int_Addr_Config, MaceEnet::IAC_PHYADDR);
        for (auto byte : station)
            check(mace->read(MaceEnet::Phys_Addr) == byte, "MACE powers up with the PROM address");

        // Put DMA away from address zero to check base-register addressing.
        write(AMICReg::DMA_Base_Addr_1, 4);
        auto dma = ram + 0x40000;
        mace->write(MaceEnet::MAC_Config_Ctrl, 3);
        std::vector<uint8_t> frame(1514);
        for (unsigned i = 0; i < frame.size(); ++i)
            frame[i] = i;
        std::memcpy(frame.data(), station, 6);
        auto ack_rx = [&]() {
            mace->read(MaceEnet::Interrupt);
            write(AMICReg::Enet_DMA_Rcv_Ctrl, 0x82);
            write(AMICReg::Enet_Rcv_Tail, read(AMICReg::Enet_Rcv_Head));
            write(AMICReg::Enet_DMA_Rcv_Ctrl, 0x0A);
        };
        wire->inject(frame);
        check(read(AMICReg::Enet_Rcv_Head) == 0, "stopped RX DMA ignores frames");
        write(AMICReg::Enet_DMA_Rcv_Ctrl, 0x02); // RUN, interrupts masked
        wire->inject(frame);
        check(read(AMICReg::Enet_Rcv_Head) == 6 && dma[0] == 5 && dma[1] == 0xEE,
              "RX advances in pages and counts the wire FCS");
        check(std::memcmp(dma + 8, frame.data(), frame.size()) == 0,
              "RX DMA stores the whole Ethernet frame after its status");
        const uint8_t fcs[] = {0xFA, 0xA6, 0x63, 0xDE};
        check(std::memcmp(dma + 8 + frame.size(), fcs, 4) == 0,
              "RX appends a valid Ethernet CRC in wire byte order");
        check(!int_pin && read(AMICReg::DMA_IFR_0) == 0, "masked RX completion remains pending");
        write(AMICReg::Enet_DMA_Rcv_Ctrl, 0x0A);
        check(int_pin && read(AMICReg::DMA_IFR_0) == 0x10, "unmasking RX asserts the DMA cascade");
        ack_rx();
        check(!int_pin, "RX acknowledgement releases the interrupt");

        // Exercise a packet split across the end of the 48 KiB ring.
        write(AMICReg::Enet_DMA_Rcv_Ctrl, 1);
        write(AMICReg::Enet_DMA_Rcv_Ctrl, 0x0A);
        auto medium = frame;
        medium.resize(1000);
        for (unsigned i = 0; i < 47; ++i) {
            wire->inject(medium);
            ack_rx();
        }
        check(read(AMICReg::Enet_Rcv_Head) == 188, "receive ring reaches its final four pages");
        wire->inject(frame);
        check(read(AMICReg::Enet_Rcv_Head) == 2 && dma[0xBC00] == 5 && dma[0xBC01] == 0xEE,
              "wrapped frame retains its header at the old head");
        check(std::memcmp(dma + 0xBC08, frame.data(), 1016) == 0 &&
              std::memcmp(dma, frame.data() + 1016, frame.size() - 1016) == 0,
              "wrapped packet payload survives across the ring boundary");
        ack_rx();

        // A full ring must never overwrite packets the guest hasn't consumed.
        write(AMICReg::Enet_DMA_Rcv_Ctrl, 1);
        write(AMICReg::Enet_DMA_Rcv_Ctrl, 0x0A);
        auto small = frame;
        small.resize(60);
        for (unsigned i = 0; i < 191; ++i)
            wire->inject(small);
        std::vector<uint8_t> before(dma, dma + 0xC000);
        wire->inject(frame);
        check(read(AMICReg::Enet_Rcv_Head) == 191 &&
              (read(AMICReg::Enet_DMA_Rcv_Ctrl) & 0xC0) == 0xC0 &&
              std::memcmp(dma, before.data(), before.size()) == 0,
              "RX overrun preserves unread packets and reports an error");
        write(AMICReg::Enet_DMA_Rcv_Ctrl, 1);
        mace->read(MaceEnet::Interrupt);
        check(!int_pin && read(AMICReg::Enet_Rcv_Head) == 0 &&
              read(AMICReg::Enet_Rcv_Tail) == 0, "RX reset recovers from overrun");
        write(AMICReg::Enet_DMA_Rcv_Ctrl, 0x0A);
        wire->inject(small);
        check(read(AMICReg::Enet_Rcv_Head) == 1, "receive resumes after reset");
        ack_rx();

        // MkLinux uses buffer 1; also exercise buffer 0 and the count high byte.
        for (unsigned set = 0; set < 2; ++set) {
            std::memcpy(dma + 0x14000 + set * 0x800, frame.data(), frame.size());
            write(AMICReg::Enet_Xmt_Count0_Hi + set * 16, frame.size() >> 8);
            write(AMICReg::Enet_Xmt_Count0_Lo + set * 16, frame.size() & 255);
            write(AMICReg::Enet_DMA_Xmt_Ctrl, 0x0A);
            check(wire->sent.size() == set + 1 && wire->sent.back() == frame,
                  "TX sends exactly the programmed buffer to the backend");
            unsigned done = 0x80 | (0x20 << set);
            check((read(AMICReg::Enet_DMA_Xmt_Ctrl) & 0xE0) == done &&
                  read(AMICReg::DMA_IFR_0) == 0x20 && int_pin,
                  "TX completion reports its buffer and asserts DMA IRQ");
            check(mace->read(MaceEnet::Xmit_Frame_Stat) == 0x80 &&
                  mace->read(MaceEnet::Interrupt) == MaceEnet::IR_XMTINT,
                  "MACE reports a valid transmitted frame");
            write(AMICReg::Enet_DMA_Xmt_Ctrl, 0x80);
            check(!int_pin, "TX acknowledgement releases DMA IRQ");
            write(AMICReg::Enet_DMA_Xmt_Ctrl, 0x0A);
            check(wire->sent.size() == set + 1, "completed TX count cannot retransmit stale data");
        }
        write(AMICReg::Enet_DMA_Xmt_Ctrl, 1);

        // The MACE interrupt mask must also act on flags already pending.
        mace->transmit_frame(frame.data(), frame.size());
        check(!int_pin, "MACE masked completion does not assert its IRQ");
        mace->write(MaceEnet::Interrupt_Mask, 0xFE);
        check(int_pin && read(AMICReg::Int_Ctrl) == 0x88, "unmasking pending MACE TX asserts native IRQ");
        mace->write(MaceEnet::Interrupt_Mask, 0xFF);
        check(!int_pin, "masking MACE releases native IRQ");
        mace->write(MaceEnet::Interrupt_Mask, 0xFE);
        mace->read(MaceEnet::Interrupt);
        check(!int_pin, "reading MACE interrupt status clears the request");

        // The shared MACE still handles the later machines' descriptor DMA.
        MaceController pci_mace(MACE_ID_REV_A2);
        PacketChannel channel;
        auto pci_backend = std::make_unique<WireProbe>();
        auto pci_wire = pci_backend.get();
        pci_mace.set_mac_address(station);
        pci_mace.set_backend(std::move(pci_backend));
        pci_mace.connect(&channel);
        pci_mace.write(MaceEnet::MAC_Config_Ctrl, 3);
        pci_mace.xfer_to(&channel, frame.data(), 700);
        check(pci_wire->sent.empty(), "DBDMA partial TX waits for the last descriptor");
        channel.last = true;
        pci_mace.xfer_to(&channel, frame.data() + 700, frame.size() - 700);
        check(pci_wire->sent.size() == 1 && pci_wire->sent[0] == frame,
              "DBDMA assembles a frame across transmit descriptors");
        pci_wire->inject(frame);
        std::vector<uint8_t> rx(1600);
        int moved = pci_mace.xfer_from(&channel, rx.data(), rx.size());
        check(channel.ready && channel.status == MACE_DBDMA_EOP && moved == 1518 &&
              std::memcmp(rx.data(), frame.data(), frame.size()) == 0 &&
              rx[1514] == 0xEA && rx[1515] == 5,
              "DBDMA RX keeps its trailing status and early end-of-packet behavior");
    }
    gMachineObj.reset();
    std::printf("AMIC tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
