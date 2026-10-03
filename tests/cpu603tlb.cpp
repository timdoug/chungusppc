// Check the 603's software-loaded TLBs: miss exceptions and their registers,
// tlbli/tlbld, the changed-bit store miss, way selection, tlbie and TGPRs.
#include <core/timermanager.h>
#include <cpu/ppc/ppcemu.h>
#include <cpu/ppc/ppcmmu.h>
#include <devices/memctrl/memctrlbase.h>
#include <loguru.hpp>
#include <cstdio>
#include <initializer_list>

static uint8_t *ram;

static void put(uint32_t addr, std::initializer_list<uint32_t> words) {
    for (uint32_t word : words) {
        ram[addr++] = word >> 24;
        ram[addr++] = word >> 16;
        ram[addr++] = word >> 8;
        ram[addr++] = word;
    }
}

static uint32_t get(uint32_t addr) {
    return (ram[addr] << 24) | (ram[addr + 1] << 16) | (ram[addr + 2] << 8) | ram[addr + 3];
}

// Miss handler that maps the missed page with a fixed RPA and returns.
static void put_handler(uint32_t vector, bool itlb, uint16_t rpa) {
    put(vector, {
        itlb ? 0x7C74F2A6u : 0x7C70F2A6u, // mfspr r3,IMISS / DMISS
        0x38200000u,                      // li    r1,0
        0x60210000u | rpa,                // ori   r1,r1,rpa
        0x7C36F3A6u,                      // mtspr RPA,r1
        itlb ? 0x7C001FE4u : 0x7C001FA4u, // tlbli r3 / tlbld r3
        0x4C000064u,                      // rfi
    });
}

int main() {
    loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
    MemCtrlBase memory;
    ram = memory.add_ram_region(0, 0x20000)->mem_ptr;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", name);
            ++failures;
        }
    };
    auto run_handler = [] {
        for (int i = 0; i < 6; i++)
            ppc_exec_single();
    };

    ppc_cpu_init(&memory, PPC_VER::MPC603, false, 7833600);

    // RPA = RPN | R | C | PP, with PP = 2 for read/write
    put_handler(0x1000, true,  0x5182); // code page at 0x5000
    put_handler(0x1100, false, 0x9102); // data page at 0x9000, C clear
    put_handler(0x1200, false, 0x9182); // the same page with C set
    put(0x5000, {
        0x38800042u, // 0x3000: li    r4,0x42
        0x80A60000u, // 0x3004: lwz   r5,0(r6)
        0x90A60004u, // 0x3008: stw   r5,4(r6)
        0x7C003264u, // 0x300C: tlbie r6
        0x80E60000u, // 0x3010: lwz   r7,0(r6)
        0x81480000u, // 0x3014: lwz   r10,0(r8)
        0x81690000u, // 0x3018: lwz   r11,0(r9)
        0x44000002u, // 0x301C: sc
    });
    put(0x9000, {0xDEADBEEF});

    ppc_state.sr[0] = 0x123; // VSID 0x123, Ks = Kp = 0
    ppc_state.spr[SPR::SDR1] = 0x00010000;
    ppc_msr_did_change(ppc_state.msr, MSR::ME | MSR::IR | MSR::DR, false);
    mmu_change_mode();
    ppc_state.pc = 0x3000;
    ppc_state.cr = 0xA0000000;
    for (int i = 0; i < 4; i++)
        ppc_state.gpr[i] = 0x10 + i;
    ppc_state.gpr[6] = 0x7000;
    ppc_state.gpr[8] = 0x27000; // same TLB set as 0x7000
    ppc_state.gpr[9] = 0x47000; // and again

    // Instruction fetch with nothing in the ITLB.
    ppc_exec_single();
    uint32_t srr1 = ppc_state.spr[SPR::SRR1];
    check(ppc_state.pc == 0x1000, "instruction miss vectors to 0x1000");
    check(ppc_state.spr[SPR::SRR0] == 0x3000 && ppc_state.spr[SPR::IMISS] == 0x3000,
          "SRR0 and IMISS hold the missed fetch");
    check(ppc_state.spr[SPR::ICMP] == 0x80009180, "ICMP holds the PTE word to compare");
    check(ppc_state.spr[SPR::HASH1] == 0x14800 && ppc_state.spr[SPR::HASH2] == 0x1B7C0,
          "HASH1 and HASH2 address the primary and secondary PTEGs");
    check((srr1 >> 28) == 0xA && (srr1 & 0x40000) && !(srr1 & 0x30000),
          "SRR1 holds CR0, flags an instruction miss and picks way 0");
    check((ppc_state.msr & MSR::TGPR) && !(ppc_state.msr & (MSR::IR | MSR::DR)),
          "the handler runs untranslated with MSR[TGPR] set");
    check(ppc_state.gpr[0] == 0 && ppc_state.gpr[3] == 0, "TGPR0-3 replace GPR0-3");
    run_handler();
    check(ppc_state.pc == 0x3000 && !(ppc_state.msr & MSR::TGPR), "rfi resumes the fetch");
    check(!(ppc_state.msr & 0x80000000), "rfi doesn't copy the saved CR0 into the MSR");
    check(ppc_state.gpr[0] == 0x10 && ppc_state.gpr[1] == 0x11 && ppc_state.gpr[3] == 0x13,
          "rfi brings GPR0-3 back");
    ppc_exec_single();
    check(ppc_state.gpr[4] == 0x42, "the loaded ITLB entry maps the code page");

    // Data load with nothing in the DTLB.
    ppc_exec_single();
    srr1 = ppc_state.spr[SPR::SRR1];
    check(ppc_state.pc == 0x1100 && ppc_state.spr[SPR::SRR0] == 0x3004 &&
          ppc_state.spr[SPR::DMISS] == 0x7000, "data load miss vectors to 0x1100");
    check(!(srr1 & 0x70000), "SRR1 flags a data load and picks way 0");
    check(ppc_state.gpr[1] == 0x5182 && ppc_state.gpr[3] == 0x3000,
          "TGPRs keep their values between misses");
    run_handler();
    ppc_exec_single();
    check(ppc_state.gpr[5] == 0xDEADBEEF, "the loaded DTLB entry maps the data page");

    // Store through the entry the load handler left with C clear.
    ppc_exec_single();
    srr1 = ppc_state.spr[SPR::SRR1];
    check(ppc_state.pc == 0x1200 && ppc_state.spr[SPR::DMISS] == 0x7004,
          "a store through an entry with C clear takes the store miss");
    check((srr1 & 0x10000) && !(srr1 & 0x60000),
          "SRR1 flags a store and names the way that hit");
    run_handler();
    ppc_exec_single();
    check(ppc_state.pc == 0x300C && get(0x9004) == 0xDEADBEEF,
          "the store completes once the entry has C set");

    // tlbie empties the set, so the page misses again.
    ppc_exec_single();
    ppc_exec_single();
    check(ppc_state.pc == 0x1100 && ppc_state.spr[SPR::DMISS] == 0x7000, "tlbie drops the entry");
    run_handler();
    ppc_exec_single();
    check(ppc_state.gpr[7] == 0xDEADBEEF, "the page maps again after reloading");

    // Way selection within one set: a free way first, then the older entry.
    ppc_exec_single();
    check(ppc_state.pc == 0x1100 && ((ppc_state.spr[SPR::SRR1] >> 17) & 1) == 1,
          "a second page in the set goes to the free way");
    run_handler();
    ppc_exec_single();
    check(ppc_state.gpr[10] == 0xDEADBEEF, "both ways translate");
    ppc_exec_single();
    check(ppc_state.pc == 0x1100 && ((ppc_state.spr[SPR::SRR1] >> 17) & 1) == 0,
          "a third page replaces the least recently used way");
    run_handler();
    ppc_exec_single();
    check(ppc_state.gpr[11] == 0xDEADBEEF && ppc_state.pc == 0x301C,
          "the replacement entry translates");

    // An OS may restore a system call's SRR1 as its MSR, so it mustn't set TGPR.
    ppc_exec_single();
    check(ppc_state.pc == 0xC00 && !(ppc_state.spr[SPR::SRR1] & 0xFFFF0000),
          "sc leaves the upper half of SRR1 clear");

    std::printf("603 TLB: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
