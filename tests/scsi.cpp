// NCR53C94 transfers split across AMIC DMA buffers, as in Mach's scatter/gather I/O.
#include <core/timermanager.h>
#include <cpu/ppc/ppcemu.h>
#include <devices/common/scsi/sc53c94.h>
#include <devices/ioctrl/amic.h>
#include <devices/memctrl/memctrlbase.h>
#include <machines/machinebase.h>
#include <machines/machineproperties.h>
#include <loguru.hpp>
#include <cstdio>
#include <cstring>
#include <vector>

struct ScsiInterruptProbe : HWComponent, InterruptCtrl {
    ScsiInterruptProbe() { supports_types(HWCompType::INT_CTRL); }
    uint64_t register_dev_int(IntSrc) override { return 1; }
    uint64_t register_dma_int(IntSrc) override { return 2; }
    void ack_int(uint64_t, uint8_t) override {}
    void ack_dma_int(uint64_t, uint8_t) override {}
};
struct ScsiMemoryTarget : ScsiPhysDevice {
    std::vector<uint8_t> bytes;
    ScsiMemoryTarget() : ScsiPhysDevice("memory target", 0), bytes(65536) {
        set_read_more_data_cb([](int*, uint8_t**) { return false; });
        set_write_more_data_cb([](int*, uint8_t**) { return false; });
    }
    void process_command() override {
        set_buffer(bytes.data());
        set_xfer_len(bytes.size());
        status = ScsiStatus::GOOD;
        msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;
        switch_phase(cmd_buf[0] == 8 ? ScsiPhase::DATA_IN : ScsiPhase::DATA_OUT);
    }
};
int main() {
    loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
    uint64_t now = 0;
    auto tm = TimerManager::get_instance();
    tm->set_time_now_cb([&] { return now; });
    tm->set_notify_changes_cb([] {});
    auto advance = [&] { now += 1000000; tm->process_timers(); };
    gMachineSettings["hdd_img"] = std::make_unique<StrProperty>("");
    gMachineSettings["cdr_img"] = std::make_unique<StrProperty>("");
    gMachineObj = std::make_unique<MachineBase>("SCSI test");
    auto bus_owner = std::make_unique<ScsiBus>("ScsiCurio");
    auto bus = bus_owner.get();
    gMachineObj->add_device("ScsiCurio", std::move(bus_owner));
    gMachineObj->add_device("interrupt", std::make_unique<ScsiInterruptProbe>());
    Sc53C94 scsi;
    scsi.device_postinit();
    ScsiMemoryTarget target;
    bus->register_device(0, &target);
    MemCtrlBase memory;
    if (!memory.add_ram_region(0, 0x18000) ||
        !memory.add_ram_region(0x18000, 0x28000)) return 1;
    auto ram = [&](unsigned offset) -> uint8_t& {
        return *memory.get_region_hostmem_ptr(offset);
    };
    mem_ctrl_instance = &memory;
    AmicScsiDma dma;
    dma.connect(&scsi);
    scsi.connect(&dma);
    int failures = 0, checks = 0;
    auto check = [&](bool ok, const char* name) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("FAIL %s\n", name);
        }
    };
    for (bool input : {true, false}) {
        for (unsigned fragment : {8U, 4096U, 65536U}) {
            for (unsigned i = 0; i < target.bytes.size(); ++i) {
                auto pattern = uint8_t(i * 37 + i / 251);
                target.bytes[i] = input ? pattern : 0xCC;
                ram(0x10000 + i) = input ? 0xDD : pattern;
            }
            scsi.write(Write::Reg53C94::Command, CMD_CLEAR_FIFO);
            scsi.write(Write::Reg53C94::Config_1, 7);
            scsi.write(Write::Reg53C94::Dest_Bus_ID, 0);
            for (uint8_t byte : {uint8_t(input ? 8 : 10), uint8_t(0), uint8_t(0), uint8_t(0), uint8_t(128), uint8_t(0)})
                scsi.write(Write::Reg53C94::FIFO, byte);
            scsi.write(Write::Reg53C94::Command, CMD_SELECT_NO_ATN);
            for (int n = 0; n < 10; ++n) advance();
            check(bus->current_phase() == (input ? ScsiPhase::DATA_IN : ScsiPhase::DATA_OUT), "selection reaches data phase");
            scsi.read(Read::Reg53C94::Int_Status);
            for (unsigned offset = 0; offset < target.bytes.size(); offset += fragment) {
                unsigned address = 0x10000 + offset;
                dma.reset(address);
                dma.write_ctrl(1);
                scsi.write(Write::Reg53C94::Xfer_Cnt_LSB, fragment & 255);
                scsi.write(Write::Reg53C94::Xfer_Cnt_MSB, (fragment >> 8) & 255);
                scsi.write(Write::Reg53C94::Command, CMD_XFER | CMD_ISDMA);
                dma.reinit(address);
                if (input) dma.xfer_from_device(); else dma.xfer_to_device();
                dma.write_ctrl(input ? 2 : 0x42);
                check(scsi.read(Read::Reg53C94::Xfer_Cnt_LSB) == 0 && scsi.read(Read::Reg53C94::Xfer_Cnt_MSB) == 0, "DMA consumes fragment");
                if (input) {
                    dma.reinit(address);
                    dma.xfer_from_device();
                    dma.write_ctrl(0x12);
                }
                dma.write_ctrl(input ? 0 : 0x40);
                scsi.read(Read::Reg53C94::Int_Status);
            }
            bool same = true;
            for (unsigned i = 0; i < target.bytes.size(); ++i)
                same &= ram(0x10000 + i) == target.bytes[i];
            check(same, "all bytes match across the host RAM allocation boundary");
            check(bus->current_phase() == ScsiPhase::STATUS, "last fragment reaches status");
            scsi.write(Write::Reg53C94::Command, CMD_COMPLETE_STEPS);
            check(scsi.read(Read::Reg53C94::FIFO) == 0, "good status");
            check(scsi.read(Read::Reg53C94::FIFO) == 0, "command complete message");
            scsi.read(Read::Reg53C94::Int_Status);
            scsi.write(Write::Reg53C94::Command, CMD_MSG_ACCEPTED);
            scsi.read(Read::Reg53C94::Int_Status);
        }
    }
    std::printf("SCSI: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
