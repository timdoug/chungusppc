// Exercise the AMIC register sequences used by Mac OS and MkLinux.
#include <core/timermanager.h>
#include <cpu/ppc/ppcemu.h>
#include <devices/common/scsi/sc53c94.h>
#include <devices/floppy/swim3.h>
#include <devices/ioctrl/amic.h>
#include <machines/machinebase.h>
#include <machines/machineproperties.h>
#include <loguru.hpp>
#include <cstdio>

int main() {
    loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
    auto tm = TimerManager::get_instance();
    tm->set_time_now_cb([]() { return uint64_t(0); });
    tm->set_notify_changes_cb([]() {});
    gMachineSettings["mon_id"] = std::make_unique<StrProperty>("VGA-SVGA");
    gMachineObj = std::make_unique<MachineBase>("AMIC regression test");
    gMachineObj->add_device("Sc53C94", std::make_unique<Sc53C94>());
    gMachineObj->add_device("Swim3", std::make_unique<Swim3::Swim3Ctrl>());
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
    }
    gMachineObj.reset();
    std::printf("AMIC tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
