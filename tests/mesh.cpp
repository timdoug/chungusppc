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

#include <core/timermanager.h>
#include <devices/common/scsi/mesh.h>
#include <loguru.hpp>

#include <cstdio>

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
    ScsiBus bus("empty test bus");
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
    std::printf("MESH: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
