// Apple's documented sparse RAM ranges must never share backing storage.
#include <devices/memctrl/hmc.h>
#include <loguru.hpp>
#include <cstdio>
#include <vector>

int main() {
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* name) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("FAIL %s\n", name);
        }
    };
    for (unsigned count : {4U, 8U}) {
        for (unsigned size : {1U, 2U, 4U, 8U, 16U, 32U}) {
            HMC hmc;
            check(!hmc.install_ram_banks(std::vector<uint32_t>(count, size << 20)), "install RAM");
            auto pointer = [&](uint32_t address) { return hmc.get_region_hostmem_ptr(address); };
            check(pointer(0) && pointer(0x007FFFFF), "eight MiB motherboard RAM");
            check(!pointer(0x00800000), "gap after motherboard RAM");
            *pointer(0) = 0xCC;
            for (unsigned bank = 0; bank < count; ++bank) {
                uint32_t start = 0x01000000 + bank * 0x04000000;
                check(!pointer(start - 1), "gap before expansion bank");
                check(pointer(start) && pointer(start + (size << 20) - 1), "bank extent");
                check(!pointer(start + (size << 20)), "gap after expansion bank");
                *pointer(start) = bank;
                *pointer(start + (size << 20) - 1) = bank + 0x80;
            }
            // Check after writing all banks: aliases would overwrite these markers.
            check(*pointer(0) == 0xCC, "motherboard RAM is independent");
            for (unsigned bank = 0; bank < count; ++bank) {
                uint32_t start = 0x01000000 + bank * 0x04000000;
                check(*pointer(start) == bank, "bank starts are independent");
                check(*pointer(start + (size << 20) - 1) == bank + 0x80,
                      "bank ends are independent");
            }
            check(!pointer(0x10000000), "no phantom bank A alias");
            hmc.write(0, 8, 0, 1);
            for (unsigned bit = 0; bit < HMC_CTRL_BITS; ++bit)
                hmc.write(0, 0, bit == HMC_L2_EN, 1);
            for (unsigned round = 0; round < 3; ++round)
                for (unsigned bit = 0; bit < HMC_CTRL_BITS; ++bit)
                    check(hmc.read(0, 0, 1) == (bit == HMC_L2_EN), "control read wraps at 35 bits");
        }
    }
    {
        HMC compact;
        check(!compact.install_ram(8 << 20, 32 << 20, 32 << 20), "6100 RAM sizing layout");
        auto a = compact.get_region_hostmem_ptr(0x00800000);
        auto b = compact.get_region_hostmem_ptr(0x08000000);
        check(a == compact.get_region_hostmem_ptr(0x10000000), "6100 sizing alias");
        *a = 0xA1;
        *b = 0xB2;
        compact.write(0, 8, 0, 1);
        uint64_t value = uint64_t(BANK_CFG_32MB) << HMC_RAM_CFG_POS;
        for (unsigned bit = 0; bit < HMC_CTRL_BITS; ++bit)
            compact.write(0, 0, (value >> bit) & 1, 1);
        check(compact.get_region_hostmem_ptr(0x02800000) == b, "6100 compact bank B position");
        check(*a == 0xA1 && *b == 0xB2, "6100 banks remain distinct after remapping");
        check(!compact.get_region_hostmem_ptr(0x08000000), "6100 old bank position removed");
    }
    HMC sparse;
    check(!sparse.install_ram_banks({0, 32 << 20}), "empty first bank");
    check(!sparse.get_region_hostmem_ptr(0x01000000), "empty bank stays absent");
    check(sparse.get_region_hostmem_ptr(0x05000000), "later populated bank remains present");
    HMC invalid;
    check(invalid.install_ram_banks({64 << 20}) != 0, "reject bank larger than physical window");
    check(invalid.install_ram_banks({3 << 20}) != 0, "reject non-power-of-two bank");
    check(invalid.install_ram_banks(std::vector<uint32_t>(9, 0)) != 0, "reject too many banks");
    std::printf("HMC: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
