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

#include <core/memaccess.h>
#include <core/timermanager.h>
#include <cpu/ppc/ppcemu.h>
#include <devices/common/dbdma.h>
#include <devices/common/scsi/mesh.h>
#include <devices/memctrl/memctrlbase.h>
#include <loguru.hpp>

#include <cstdio>
#include <cstring>
#include <vector>

// CHRP I/O Device Reference, sections 12.5.4, 12.5.8, and 12.5.10-12.5.11.
// Exercise actual MESH registers, bus sequencing, and IRQ delivery without a ROM.
using namespace MeshScsi;

struct InterruptProbe : InterruptCtrl {
    uint8_t line = 0;
    uint64_t register_dev_int(IntSrc) override { return 1; }
    uint64_t register_dma_int(IntSrc) override { return 2; }
    void ack_int(uint64_t, uint8_t state) override { line = state; }
    void ack_dma_int(uint64_t, uint8_t) override {}
};

struct MeshProbe : MeshController {
    MeshProbe(ScsiBus &bus, InterruptProbe &intr) : MeshController(TntMeshID) {
        bus_obj = &bus;
        int_ctrl = &intr;
        irq_id = 1;
        bus.register_device(7, this);
    }
};

struct MemoryTarget : ScsiPhysDevice {
    std::vector<uint8_t> bytes;
    unsigned length = 512;
    MemoryTarget() : ScsiPhysDevice("memory disk", 0), bytes(65536) {
        set_read_more_data_cb([](int*, uint8_t**) { return false; });
        set_write_more_data_cb([](int*, uint8_t**) { return false; });
    }
    void process_command() override {
        set_buffer(bytes.data());
        set_xfer_len(length);
        status = ScsiStatus::GOOD;
        msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;
        switch_phase(cmd_buf[0] == 0x08 ? ScsiPhase::DATA_IN : ScsiPhase::DATA_OUT);
    }
};

int main() {
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
    uint64_t now = 0;
    int failures = 0, checks = 0;
    auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    };
    auto tm = TimerManager::get_instance();
    tm->set_time_now_cb([&]() { return now; });
    tm->set_notify_changes_cb([]() {});
    auto advance = [&](uint64_t ns) {
        now += ns;
        tm->process_timers();
    };
    ScsiBus bus("test bus");
    InterruptProbe irq;
    MeshProbe mesh(bus, irq);
    mesh.write(MeshReg::Sequence, SeqCmd::ResetMesh);
    check(mesh.read(MeshReg::Interrupt) == INT_CMD_DONE && !irq.line, "reset completion is masked");
    mesh.write(MeshReg::IntMask, INT_MASK);
    check(irq.line, "unmask pending completion asserts IRQ");
    mesh.write(MeshReg::IntMask, 0);
    check(!irq.line, "mask pending completion deasserts IRQ");
    mesh.write(MeshReg::IntMask, INT_MASK);
    mesh.write(MeshReg::Sequence, SeqCmd::FlushFIFO);
    check(!irq.line && !mesh.read(MeshReg::Interrupt), "next command clears completion and IRQ");
    for (int pass = 0; pass < 3; ++pass) {
        for (int target = 0; target < 7; ++target) {
            mesh.write(MeshReg::DestID, 7);
            mesh.write(MeshReg::Sequence, SeqCmd::Arbitrate);
            check(!mesh.read(MeshReg::Interrupt), "arbitration is initially pending");
            advance(BUS_FREE_DELAY + BUS_SETTLE_DELAY);
            advance(ARB_DELAY);
            check(mesh.read(MeshReg::Interrupt) == INT_CMD_DONE && irq.line,
                  "arbitration completes");
            mesh.write(MeshReg::IntMask, 0);
            check(!irq.line, "masking an active IRQ deasserts it");
            mesh.write(MeshReg::IntMask, INT_MASK);
            check(irq.line, "unmasking an active completion reasserts IRQ");
            mesh.write(MeshReg::Sequence, SeqCmd::FlushFIFO);
            check(!irq.line && !mesh.read(MeshReg::Interrupt),
                  "new command clears a previously asserted IRQ");
            mesh.write(MeshReg::DestID, target);
            mesh.write(MeshReg::Sequence, SeqCmd::DisReselect);
            check(!mesh.read(MeshReg::Interrupt) && !irq.line,
                  "disable reselection has no completion interrupt");
            mesh.write(MeshReg::Sequence, SeqCmd::FlushFIFO);
            mesh.write(MeshReg::Sequence, SeqCmd::Select);
            advance(SEL_TIME_OUT - 1);
            check(!mesh.read(MeshReg::Interrupt), "absent target waits for selection timeout");
            advance(1);
            check(mesh.read(MeshReg::Interrupt) == (INT_EXCEPTION | INT_CMD_DONE) && irq.line,
                  "selection timeout interrupts");
            check(mesh.read(MeshReg::Exception) == EXC_SEL_TIMEOUT && bus.current_phase() == ScsiPhase::BUS_FREE,
                  "timeout releases bus");
            mesh.write(MeshReg::Interrupt, INT_EXCEPTION | INT_CMD_DONE);
            check(!mesh.read(MeshReg::Exception) && !mesh.read(MeshReg::Interrupt) && !irq.line,
                  "acknowledging timeout clears exception and IRQ");
            mesh.write(MeshReg::Sequence, SeqCmd::EnaReselect);
            check(!mesh.read(MeshReg::Interrupt) && !irq.line,
                  "enable reselection has no completion interrupt");
        }
    }
    MemoryTarget target;
    bus.register_device(0, &target);
    MemCtrlBase memory;
    auto ram = memory.add_ram_region(0, 0x20000)->mem_ptr;
    mem_ctrl_instance = &memory;
    DMAChannel dma("MESH test DMA");
    dma.register_dma_int(&irq, 2);
    dma.connect(&mesh);
    mesh.connect(&dma);
    auto count = [&](unsigned n) {
        mesh.write(MeshReg::XferCount0, n & 255);
        mesh.write(MeshReg::XferCount1, (n >> 8) & 255);
    };
    auto select = [&](bool input) {
        mesh.write(MeshReg::Sequence, SeqCmd::Arbitrate);
        advance(BUS_FREE_DELAY + BUS_SETTLE_DELAY);
        advance(ARB_DELAY);
        mesh.write(MeshReg::DestID, 0);
        mesh.write(MeshReg::Sequence, SeqCmd::Select);
        advance(BUS_SETTLE_DELAY);
        check(bus.current_phase() == ScsiPhase::COMMAND, "select populated target");
        count(6);
        mesh.write(MeshReg::Sequence, SeqCmd::Command);
        for (auto byte : {input ? 0x08 : 0x0A, 0, 0, 0, 1, 0})
            mesh.write(MeshReg::FIFO, byte);
    };
    auto finish = [&] {
        check(bus.current_phase() == ScsiPhase::STATUS &&
              mesh.read(MeshReg::Interrupt) == INT_CMD_DONE && irq.line,
              "data completion advances to status and asserts IRQ");
        count(1);
        mesh.write(MeshReg::Sequence, SeqCmd::Status);
        check(mesh.read(MeshReg::FIFO) == ScsiStatus::GOOD, "successful target status");
        count(1);
        mesh.write(MeshReg::Sequence, SeqCmd::MessageIn);
        check(mesh.read(MeshReg::FIFO) == ScsiMessage::COMMAND_COMPLETE, "command complete message");
        mesh.write(MeshReg::Sequence, SeqCmd::BusFree);
        check(bus.current_phase() == ScsiPhase::BUS_FREE, "completed command releases bus");
    };
    // Two descriptors include a seven-byte fragment, exercising partial FIFO
    // draining. Start DBDMA both before and after the MESH data command.
    for (unsigned length : {16U, 512U, 65536U}) {
        target.length = length;
        for (bool input : {false, true}) {
            for (bool dma_first : {false, true}) {
                for (unsigned i = 0; i < length; ++i) {
                    uint8_t pattern = (i * 37 + i / 251) & 255;
                    ram[0x1000 + i] = input ? 0xCC : pattern;
                    target.bytes[i] = input ? pattern : 0xDD;
                }
                select(input);
                std::memset(ram + 0x100, 0, 48);
                for (unsigned part = 0; part < 2; ++part) {
                    auto cmd = ram + 0x100 + part * 16;
                    WRITE_WORD_LE_A(cmd, part ? length - 7 : 7);
                    cmd[3] = ((input ? DBDMA_Cmd::INPUT_MORE : DBDMA_Cmd::OUTPUT_MORE) + part) << 4;
                    WRITE_DWORD_LE_A(cmd + 4, 0x1000 + (part ? 7 : 0));
                }
                ram[0x123] = DBDMA_Cmd::STOP << 4;
                auto start_dma = [&] {
                    dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0xFFFF0000U), 4);
                    dma.reg_write(DMAReg::CMD_PTR_LO, BYTESWAP_32(0x100U), 4);
                    dma.reg_write(DMAReg::CH_CTRL, BYTESWAP_32(0x80008000U), 4);
                };
                if (dma_first) {
                    start_dma();
                    check(dma.dma_is_ready() && bus.current_phase() != ScsiPhase::STATUS,
                          "DBDMA waits for MESH data command");
                }
                count(length);
                mesh.write(MeshReg::Sequence, 0x80 | (input ? SeqCmd::DataIn : SeqCmd::DataOut));
                if (!dma_first)
                    start_dma();
                check(!std::memcmp(ram + 0x1000, target.bytes.data(), length),
                      "all DMA bytes reach their destination in order");
                check(READ_WORD_LE_A(ram + 0x10C) == 0 && READ_WORD_LE_A(ram + 0x11C) == 0 &&
                      !dma.dma_is_ready(), "DBDMA completes both descriptors without residual bytes");
                check(!mesh.read(MeshReg::XferCount0) && !mesh.read(MeshReg::XferCount1),
                      "MESH transfer counter reaches zero including 64 KiB commands");
                finish();
            }
        }
    }
    // The Gazelle ROM writes full FIFO bursts, waiting for an empty FIFO
    // before sending the next burst. Include a final partial burst and the
    // zero-encoded 64 KiB transfer count.
    for (unsigned length : {28U, 512U, 65536U}) {
        target.length = length;
        std::fill(target.bytes.begin(), target.bytes.end(), 0xDD);
        select(false);
        count(length);
        mesh.write(MeshReg::Sequence, SeqCmd::DataOut);
        bool drained = true;
        for (unsigned i = 0; i < length; ++i) {
            mesh.write(MeshReg::FIFO, (i * 37 + i / 251) & 255);
            if ((i + 1) % 16 == 0 || i + 1 == length) {
                drained = !mesh.read(MeshReg::FIFOCount);
                if (!drained)
                    break;
            }
        }
        check(drained, "PIO write drains each full FIFO and the final partial burst");
        if (!drained)
            break;
        bool matched = true;
        for (unsigned i = 0; i < length; ++i)
            matched &= target.bytes[i] == ((i * 37 + i / 251) & 255);
        check(matched, "PIO write preserves every byte across FIFO boundaries");
        check(!mesh.read(MeshReg::XferCount0) && !mesh.read(MeshReg::XferCount1),
              "PIO transfer count reaches zero");
        finish();
    }
    std::printf("MESH: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
