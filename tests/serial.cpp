// ESCC interrupt delivery, independent ports, bounded DMA and socket reconnect.
#include <core/hostevents.h>
#include <core/timermanager.h>
#include <devices/serial/escc.h>
#include <devices/ioctrl/amic.h>
#include <devices/memctrl/memctrlbase.h>
#include <cpu/ppc/ppcemu.h>
#include <devices/serial/z85c30.h>
#include <machines/machinebase.h>
#include <machines/machineproperties.h>
#include <loguru.hpp>

#include <cstdio>
#include <deque>
#include <map>
#include <vector>
#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

struct SerialIrq : HWComponent, InterruptCtrl {
    SerialIrq() { supports_types(HWCompType::INT_CTRL); }
    std::map<uint64_t, bool> levels;
    uint64_t register_dev_int(IntSrc source) override {
        return source == IntSrc::ESCC ? 1 : source == IntSrc::SCCA ? 2 : 4;
    }
    uint64_t register_dma_int(IntSrc) override { return 8; }
    void ack_int(uint64_t id, uint8_t level) override { levels[id] = level; }
    void ack_dma_int(uint64_t id, uint8_t level) override { levels[id] = level; }
};

struct SerialBytes : CharIoBackEnd {
    bool enabled = false, online = true;
    bool connected() const override { return online; }
    std::deque<uint8_t> input;
    std::vector<uint8_t> output;
    int rcv_enable() override { enabled = true; return 0; }
    void rcv_disable() override { enabled = false; }
    bool rcv_char_available() override { return rcv_char_available_now(); }
    bool rcv_char_available_now() override { return enabled && !input.empty(); }
    int xmit_char(uint8_t c) override { output.push_back(c); return 0; }
    int rcv_char(uint8_t* c) override {
        if (!rcv_char_available_now()) return -1;
        *c = input.front(); input.pop_front(); return 0;
    }
};

struct DmaBytes : DmaDevice {
    std::deque<uint8_t> input;
    std::vector<uint8_t> output;
    int xfer_from(DmaChannel*, uint8_t* buf, int len) override {
        int moved = 0;
        while (moved < len && !input.empty()) {
            buf[moved++] = input.front(); input.pop_front();
        }
        return moved;
    }
    int xfer_to(DmaChannel*, uint8_t* buf, int len) override {
        if (!len) return 0;
        output.push_back(*buf); return 1; // one character per transmitter tick
    }
};

int main() {
    loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
    int checks = 0, failures = 0;
    uint64_t now = 0;
    auto timers = TimerManager::get_instance();
    timers->set_time_now_cb([&] { return now; });
    timers->set_notify_changes_cb([] {});
    auto tick = [&] { now += 300000; timers->process_timers(); };
    auto check = [&](bool ok, const char* name) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
    };
    for (bool amic : {false, true}) {
        gMachineSettings["serial_backend"] = std::make_unique<StrProperty>("null");
        gMachineSettings["serial_b_backend"] = std::make_unique<StrProperty>("null");
        gMachineObj = std::make_unique<MachineBase>("serial test");
        auto controller = std::make_unique<SerialIrq>();
        auto irq = controller.get();
        gMachineObj->add_device(amic ? "Amic" : "interrupt", std::move(controller));
        {
            EsccController escc;
            auto a = std::make_unique<SerialBytes>();
            auto b = std::make_unique<SerialBytes>();
            auto host_a = a.get(); auto host_b = b.get();
            escc.attach_backend(CH_A, std::move(a));
            escc.attach_backend(CH_B, std::move(b));
            escc.device_postinit();
            auto wr = [&](uint8_t port, uint8_t reg, uint8_t value) {
                escc.write(port, reg); escc.write(port, value);
            };
            auto rr = [&](uint8_t port, uint8_t reg) {
                escc.write(port, reg); return escc.read(port);
            };
            uint64_t ia = amic ? 1 : 2, ib = amic ? 1 : 4;
            for (auto port : {Port_A_Cmd, Port_B_Cmd}) {
                wr(port, WR4, 0x44); // 8N1, x16 clock
                wr(port, WR11, 0x50); // receive and transmit from BRG
                wr(port, WR12, 1); wr(port, WR13, 0); // 38,400 baud
            }
            wr(Port_A_Cmd, WR3, 0xC1);
            wr(Port_B_Cmd, WR3, 0xC1);
            wr(Port_A_Cmd, WR5, 0xEA);
            wr(Port_B_Cmd, WR5, 0xEA);
            wr(Port_A_Cmd, WR1, 0x12);
            wr(Port_B_Cmd, WR1, 0x10);
            check(!irq->levels[ia], "master interrupt mask holds pending transmitter");
            wr(Port_A_Cmd, WR9, 0x0A);
            check(irq->levels[ia], "enabling master interrupts asserts pending transmitter");
            check(rr(Port_A_Cmd, RR3) == 0x10, "RR3 reports channel A transmitter");
            escc.write(Port_A_Cmd, WR0_COMMAND_RESET_TXINT_PENDING);
            check(!irq->levels[ia], "transmitter acknowledgement clears interrupt");
            escc.write(Port_A_Data, 0xA5);
            check(!(escc.read(Port_A_Cmd) & RR0_TX_BUFFER_EMPTY) && !irq->levels[ia],
                  "transmitter remains busy until its character completes");
            now += 200000; timers->process_timers();
            check(!(escc.read(Port_A_Cmd) & RR0_TX_BUFFER_EMPTY),
                  "38,400 baud transmitter is still busy after 200 microseconds");
            tick();
            check(host_a->output == std::vector<uint8_t>{0xA5} && irq->levels[ia],
                  "transmission delivers byte and rearms transmitter interrupt");
            escc.write(Port_A_Cmd, WR0_COMMAND_RESET_TXINT_PENDING);
            host_a->input = {0x00, 0xFF}; host_b->input = {0x42};
            tick(); tick();
            check(irq->levels[ia] && irq->levels[ib], "host input raises both receive interrupts");
            check(rr(Port_A_Cmd, RR3) == 0x24 && rr(Port_B_Cmd, RR3) == 0,
                  "RR3 combines both ports only on channel A");
            check(escc.read(Port_A_Data) == 0 && escc.read(Port_A_Data) == 255,
                  "receive data preserves zero and high-bit bytes");
            check(amic ? irq->levels[ia] : !irq->levels[ia],
                  "draining A preserves only the pending B interrupt");
            check(escc.read(Port_B_Data) == 0x42 && !irq->levels[ib],
                  "draining B releases the final interrupt");
            host_a->input = {1,2,3,4}; tick(); tick(); tick(); tick();
            check((rr(Port_A_Cmd, RR1) & RR1_RX_OVERRUN_ERROR) && irq->levels[ia],
                  "four unread bytes overflow the three-character receiver FIFO");
            escc.read(Port_A_Data); escc.read(Port_A_Data); escc.read(Port_A_Data);
            escc.write(Port_A_Cmd, WR0_COMMAND_ERROR_RESET);
            check(!(rr(Port_A_Cmd, RR1) & RR1_RX_OVERRUN_ERROR) && !irq->levels[ia],
                  "error reset clears receive overrun and its interrupt");
            wr(Port_A_Cmd, WR1, WR1_EXT_INT_ENABLE);
            wr(Port_A_Cmd, WR15, WR15_DCD_IE | WR15_CTS_IE);
            escc.write(Port_A_Cmd, WR0_COMMAND_RESET_EXT_STATUS_INTERRUPTS);
            host_a->online = false; escc.poll();
            check(irq->levels[ia] && rr(Port_A_Cmd, RR3) == 8 &&
                  !(rr(Port_A_Cmd, RR0) & (RR0_DCD | RR0_CTS)),
                  "disconnect clears modem lines and latches external-status IRQ");
            escc.write(Port_A_Cmd, WR0_COMMAND_RESET_EXT_STATUS_INTERRUPTS);
            check(!irq->levels[ia], "external-status acknowledgement releases IRQ");
            host_a->online = true; escc.poll();
            check(irq->levels[ia] && (rr(Port_A_Cmd, RR0) & (RR0_DCD | RR0_CTS)) == (RR0_DCD | RR0_CTS),
                  "reconnecting asserts modem lines and reports carrier change");
            escc.write(Port_A_Cmd, WR0_COMMAND_RESET_EXT_STATUS_INTERRUPTS);
            wr(Port_A_Cmd, WR1, 0x08);
            host_a->input = {1,2}; tick(); tick();
            check(irq->levels[ia], "first-character mode interrupts for its first byte");
            escc.read(Port_A_Data);
            check(!irq->levels[ia], "first-character mode waits for rearming");
            escc.write(Port_A_Cmd, WR0_COMMAND_ENABLE_INT_ON_NEXT_RX_CHARACTER);
            check(irq->levels[ia], "next-character command rearms reception");
            wr(Port_A_Cmd, WR9, 0x0A | WR9_CHANNEL_RESET_A);
            check(!irq->levels[ia], "channel reset clears interrupt state");
        }
        EventManager::get_instance()->disconnect_handlers();
        gMachineObj.reset();
    }
    {
        EsccChannel channel("DMA bounds test");
        auto backend = std::make_unique<SerialBytes>();
        auto host = backend.get();
        channel.attach_backend(std::move(backend));
        channel.reset(true); channel.write_reg(WR3, 0xC1);
        DmaChannel dma; channel.set_dma_channel(DIR_RX, &dma);
        host->input = {1,2,3,4};
        tick(); tick(); tick();
        uint8_t buffer[4] = {0xAA,0,0,0xBB};
        check(channel.xfer_from(&dma, buffer+1, 2) == 2 &&
              buffer[0] == 0xAA && buffer[3] == 0xBB && channel.receive_byte() == 3 && host->input.size() == 1,
              "DMA receive respects requested length and preserves buffer guards");
    }
    {
        MemCtrlBase memory;
        auto ram = memory.add_ram_region(0, 0x80000)->mem_ptr;
        mem_ctrl_instance = &memory;
        SerialIrq irq;
        DmaBytes serial;
        AmicSerialDma tx, rx;
        tx.init(&irq, 1, 0x24000, false); tx.set_base(0x40000); tx.connect(&serial);
        rx.init(&irq, 2, 0x22000, true); rx.set_base(0x40000); rx.connect(&serial);
        ram[0x64000] = 0xA5; ram[0x64001] = 0x5A;
        tx.write_reg(8, 1); tx.write_reg(5, 2); tx.write_reg(8, 0x0A);
        check(serial.output == std::vector<uint8_t>{0xA5} && !irq.levels[1] && tx.read_reg(5) == 1,
              "AMIC TX starts at its fixed buffer and tracks remaining count");
        tx.write_reg(8, 0x1A); tx.xfer_retry();
        check(serial.output.size() == 1 && (tx.read_reg(8) & 0x20), "AMIC TX pause freezes transfer");
        tx.write_reg(8, 0x0A);
        check(serial.output == std::vector<uint8_t>({0xA5,0x5A}) && irq.levels[1] &&
              (tx.read_reg(8) & 0x82) == 0x80, "AMIC TX completion stops and interrupts");
        tx.write_reg(8, 0x80);
        check(!irq.levels[1], "AMIC TX interrupt flag is write-one-to-clear");
        rx.write_reg(8, 1); rx.write_reg(5, 1); rx.write_reg(8, 0x0E);
        serial.input = {1,2,3}; rx.xfer_retry();
        check(ram[0x62000] == 1 && rx.read_reg(3) == 1 && irq.levels[2] && rx.dma_is_ready(),
              "AMIC RX threshold interrupts without stopping continuous reception");
        rx.xfer_retry();
        check(ram[0x62001] == 2 && ram[0x62002] == 3 && rx.read_reg(3) == 3,
              "AMIC RX continues past its interrupt threshold");
        rx.write_reg(8, 0x1E); serial.input = {4}; rx.xfer_retry();
        check(serial.input.size() == 1 && rx.read_reg(3) == 3, "AMIC RX pause preserves data");
        rx.write_reg(5, 1); rx.write_reg(8, 0x8E);
        check(ram[0x62003] == 4 && irq.levels[2], "AMIC RX resume acknowledges then rearms the threshold");
        rx.write_reg(8, 1);
        check(!irq.levels[2] && !rx.dma_is_ready() && rx.read_reg(3) == 0,
              "AMIC RX reset releases IRQ and rewinds the buffer");
        rx.write_reg(5, 1); rx.write_reg(8, 0x0E);
        for (unsigned i = 0; i < 8200; ++i) serial.input.push_back(i);
        rx.xfer_retry(); rx.xfer_retry(); rx.xfer_retry();
        check(serial.input.empty() && rx.read_reg(3) == 8 && rx.read_reg(2) == 0x20 &&
              ram[0x62000] == 0 && ram[0x62007] == 7 && ram[0x63FFF] == 255,
              "AMIC receive address wraps within its fixed 8 KiB buffer");
        mem_ctrl_instance = nullptr;
    }
#ifndef _WIN32
    {
        const char* path = "serial-regression.sock";
        CharIoSocket backend(path); backend.rcv_enable();
        auto connect_peer = [&] {
            int fd = socket(AF_UNIX, SOCK_STREAM, 0);
            sockaddr_un address{}; address.sun_family = AF_UNIX;
            std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
            if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
                close(fd); return -1;
            }
            return fd;
        };
        for (int connection = 0; connection < 2; ++connection) {
            backend.rcv_enable();
            int peer = connect_peer();
            check(peer >= 0, "socket accepts initial connection and reconnect");
            if (peer < 0) break;
            uint8_t bytes[] = {0,0x7F,0x80,0xFF};
            send(peer, bytes, sizeof(bytes), 0);
            bool match = true;
            for (auto value : bytes) {
                uint8_t received = 0;
                match &= backend.rcv_char_available_now() && backend.rcv_char(&received) == 0 && received == value;
            }
            check(match, "socket receives binary bytes");
            for (auto value : bytes) backend.xmit_char(value);
            uint8_t received[4] = {};
            check(recv(peer, received, 4, MSG_WAITALL) == 4 &&
                  std::equal(std::begin(bytes), std::end(bytes), std::begin(received)),
                  "socket transmits binary bytes");
            backend.rcv_disable();
            close(peer);
            check(!backend.rcv_char_available_now(), "socket detects EOF while the receiver is disabled");
        }
    }
#endif
    std::printf("Serial tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
