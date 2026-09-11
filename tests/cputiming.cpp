// Check instruction timing against guest execution and timer deadlines.
#include <core/timermanager.h>
#include <cpu/ppc/ppcemu.h>
#include <devices/memctrl/memctrlbase.h>
#include <loguru.hpp>
#include <cstdio>

int main() {
    loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
    MemCtrlBase memory;
    auto ram = memory.add_ram_region(0, 0x2000)->mem_ptr;
    ram[0x1000] = 0x48; // b . (big endian)
    ram[0x1001] = ram[0x1002] = ram[0x1003] = 0;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", name);
            ++failures;
        }
    };
    for (uint32_t period : {16, 13, 11}) {
        ppc_cpu_init(&memory, PPC_VER::MPC601, true, 7833600, period);
        ppc_state.pc = 0x1000;
        check(!g_realtime && get_virt_time_ns() == 0, "instruction clock starts at zero");
        unsigned fired = 0;
        TimerManager::get_instance()->add_oneshot_timer(period * 3, [&] { ++fired; });
        ppc_exec_single();
        ppc_exec_single();
        check(fired == 0, "timer does not fire early");
        ppc_exec_single();
        check(fired == 1 && get_virt_time_ns() == period * 3,
              "timer expires after three guest instructions");

        auto before = get_virt_time_ns();
        increment_icnt_factor();
        check(get_virt_time_ns() == before, "slowing clock preserves exact time");
        ppc_exec_single();
        check(get_virt_time_ns() == before + period * 2, "speed key doubles instruction time");
        before = get_virt_time_ns();
        decrement_icnt_factor();
        check(get_virt_time_ns() == before, "restoring speed preserves exact time");
        ppc_exec_single();
        check(get_virt_time_ns() == before + period, "restores model instruction period");

        for (int i = 0; i < 5; ++i)
            decrement_icnt_factor();
        before = get_virt_time_ns();
        ppc_exec_single();
        check(get_icnt_factor() == 0 && get_virt_time_ns() == before + 1,
              "fastest setting advances by one nanosecond");
        for (int i = 0; i < 4; ++i)
            increment_icnt_factor();
        before = get_virt_time_ns();
        ppc_exec_single();
        check(get_virt_time_ns() == before + period, "speed round trip retains odd periods");

        // Exercise the main interpreter's conversion from a timer deadline
        // to an instruction budget, not just the debugger's single-step path.
        before = get_virt_time_ns();
        TimerManager::get_instance()->add_oneshot_timer(1000, [] { power_on = false; });
        power_on = true;
        ppc_exec();
        check(get_virt_time_ns() >= before + 1000 &&
              get_virt_time_ns() <= before + 1000 + 2 * period,
              "interpreter services timer within two instructions of its deadline");
    }
    std::printf("CPU timing: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
