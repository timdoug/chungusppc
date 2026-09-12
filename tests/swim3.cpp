// Exercise MFM DMA reads/writes through SWIM3 registers and real raw images.
#include <core/timermanager.h>
#include <devices/common/dmacore.h>
#include <devices/common/hwinterrupt.h>
#include <devices/floppy/swim3.h>
#include <machines/machinebase.h>
#include <machines/machineproperties.h>
#include <loguru.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

struct FloppyIrq : HWComponent, InterruptCtrl {
    FloppyIrq() { supports_types(HWCompType::INT_CTRL); }
    bool asserted = false;
    uint64_t register_dev_int(IntSrc) override { return 1; }
    uint64_t register_dma_int(IntSrc) override { return 2; }
    void ack_int(uint64_t, uint8_t level) override { asserted = level; }
    void ack_dma_int(uint64_t, uint8_t) override {}
};

struct FloppyDma : DmaBidirChannel {
    FloppyDma() : DmaBidirChannel("test floppy") {}
    std::vector<uint8_t> source, received;
    size_t position = 0;
    DmaPullResult pull_data(uint32_t requested, uint32_t* count, uint8_t** data) override {
        *count = std::min(size_t(requested), source.size() - position);
        *data = source.data() + position;
        position += *count;
        return *count ? MoreData : NoMoreData;
    }
    DmaPushResult push_data(const char* data, int count) override {
        received.insert(received.end(), data, data + count);
        return PushedData;
    }
};

int main() {
    using namespace Swim3;
    loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
    auto tm = TimerManager::get_instance();
    uint64_t now = 0;
    tm->set_time_now_cb([&] { return now; });
    tm->set_notify_changes_cb([] {});
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* name) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
    };
    const char* filename = "swim3-regression.img";
    for (int sectors : {1440, 2880}) {
        std::vector<char> initial(sectors * 512, 0x55);
        { std::ofstream file(filename, std::ios::binary); file.write(initial.data(), initial.size()); }
        check(is_floppy_image(filename), "raw MFM image is recognized for insertion");
        for (bool protected_disk : {false, true}) {
            gMachineSettings["fdd_img"] = std::make_unique<StrProperty>(filename);
            gMachineSettings["fdd_fmt"] = std::make_unique<StrProperty>("");
            gMachineSettings["fdd_wr_prot"] = std::make_unique<BinProperty>(protected_disk);
            gMachineObj = std::make_unique<MachineBase>("floppy regression test");
            auto interrupt = std::make_unique<FloppyIrq>();
            auto irq = interrupt.get();
            gMachineObj->add_device("interrupt", std::move(interrupt));
            {
                Swim3Ctrl swim;
                FloppyDma dma;
                swim.set_dma_channel(&dma);
                swim.device_postinit();
                auto write = [&](Swim3Reg reg, uint8_t value) { swim.write(reg, value); };
                auto read = [&](Swim3Reg reg) { return swim.read(reg); };
                write(Handshake_Mode1, SWIM3_DRIVE_1 | SWIM3_INT_ENA);
                write(Phase, 0x0A); // motor on
                write(Phase, 4);    // select head zero
                read(Handshake_Mode1);
                write(Interrupt_Mask, INT_SECT_DONE | INT_ERROR);
                auto transfer = [&] {
                    write(First_Sector, 1);
                    write(Sectors_To_Xfer, 1);
                    write(Handshake_Mode1, SWIM3_GO);
                    for (int i = 0; i < 100 && (read(Status_Mode0) & SWIM3_GO); ++i) {
                        uint64_t delay = tm->process_timers();
                        now += delay ? delay : 1;
                    }
                    tm->process_timers();
                    check(!(read(Status_Mode0) & SWIM3_GO), "transfer clears GO");
                };
                transfer();
                check(dma.received == std::vector<uint8_t>(512, protected_disk ? 0xA5 : 0x55),
                      "read DMA retrieves the persisted sector");
                check(irq->asserted, "read completion asserts IRQ");
                write(Interrupt_Mask, 0);
                check(!irq->asserted, "masking a pending interrupt releases IRQ");
                write(Interrupt_Mask, INT_SECT_DONE | INT_ERROR);
                check(irq->asserted, "unmasking a pending interrupt asserts IRQ");
                write(Status_Mode0, SWIM3_INT_ENA);
                check(!irq->asserted, "disabling global interrupts releases IRQ");
                write(Handshake_Mode1, SWIM3_INT_ENA);
                check(irq->asserted && (read(Interrupt_Flags) & INT_SECT_DONE),
                      "completed read interrupts the guest");
                check(!irq->asserted, "reading interrupt flags acknowledges completion");

                // HALWriteSector's gap, mark, escape-disable, data and CRC sequence.
                dma.source.assign(22, 0x4E);
                dma.source.insert(dma.source.end(), 12, 0);
                for (auto b : {0x99,0xA1,0x99,0xA1,0x99,0xA1,0x99,0xFB,0x99,0x0F})
                    dma.source.push_back(b);
                dma.source.insert(dma.source.end(), 512, protected_disk ? 0xCC : 0xA5);
                for (auto b : {0x99,0x04,0x4E,0x4E,0x4E,0x4E,0x4E,0x4E,0x99,0x08})
                    dma.source.push_back(b);
                write(Handshake_Mode1, SWIM3_WR_MODE);
                transfer();
                check(irq->asserted, "write completion or rejection interrupts the guest");
                check(read(Interrupt_Flags) & (protected_disk ? INT_ERROR : INT_SECT_DONE),
                      "write protection rejects writes with an error");
                std::vector<char> persisted(initial.size());
                { std::ifstream file(filename, std::ios::binary); file.read(persisted.data(), persisted.size()); }
                check(std::all_of(persisted.begin(), persisted.begin() + 512,
                                  [](char b) { return uint8_t(b) == 0xA5; }),
                      "raw image receives the writable sector and survives write protection");
                check(std::equal(persisted.begin() + 512, persisted.end(), initial.begin() + 512),
                      "sector writes preserve the rest of the image");
                dma.position = 0;
                dma.source.resize(80); // Incomplete payload must never commit a partial sector.
                transfer();
                check(read(Interrupt_Flags) & INT_ERROR, "truncated write reports underrun");
                check(read(Error) & 1, "underrun reason is available to the driver");
                swim.write(Status_Mode0, 0xFF);
                FloppyImageEvent occupied;
                occupied.image_path = filename;
                EventManager::get_instance()->post_floppy_event(occupied);
                check(occupied.handled && !occupied.inserted, "insertion refuses occupied drive");
                write(Handshake_Mode1, SWIM3_DRIVE_1);
                write(Phase, 0x0F); // eject
                FloppyImageEvent replacement;
                replacement.image_path = filename;
                EventManager::get_instance()->post_floppy_event(replacement);
                check(replacement.inserted, "ejected media can be inserted again");
                check(read(Interrupt_Flags) & INT_SENSE, "insertion reports a sense change");
                swim.write(Status_Mode0, 0xFF);
            }
            EventManager::get_instance()->disconnect_handlers();
            gMachineObj.reset();
        }
    }
    std::remove(filename);
    std::printf("SWIM3 tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
