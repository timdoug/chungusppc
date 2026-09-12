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

#include <devices/deviceregistry.h>
#include <machines/machinefactory.h>
#include <machines/machineproperties.h>
#include <loguru.hpp>

#include <cstdio>
#include <map>
#include <optional>
#include <string>

int main() {
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
    std::map<std::string, std::string> overrides;
    MachineFactory::get_setting_value = [&](const std::string& name) -> std::optional<std::string> {
        auto it = overrides.find(name);
        return it == overrides.end() ? std::nullopt : std::optional<std::string>(it->second);
    };
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* name) {
        ++checks;
        if (!ok) {
            std::printf("FAIL %s\n", name);
            ++failures;
        }
    };
    for (const auto* model : {"pm6500", "pm5500", "tam"}) {
        check(MachineFactory::register_machine_settings(model) == 0, "register Gazelle model");
        check(GET_STR_PROP("pci_F1") == "AtiRageGT", "board supplies built-in ATI graphics");
        MachineFactory::register_device_settings("PsxPci1");
        check(GET_STR_PROP("pci_F1") == "AtiRageGT", "later device registration preserves board default");
        check(GET_STR_PROP("pci_A1").empty(), "ordinary expansion slots remain empty");
    }
    for (const auto* gpu : {"AtiRagePro", ""}) {
        overrides = {{"pci_F1", gpu}, {"rambank1_size", "64"}};
        MachineFactory::register_machine_settings("pm6500");
        check(GET_STR_PROP("pci_F1") == gpu, "explicit GPU override, including empty, wins");
        check(GET_INT_PROP("rambank1_size") == 64, "integer command-line override survives");
    }
    overrides.clear();
    MachineFactory::register_machine_settings("pm6400");
    check(GET_STR_PROP("pci_F1").empty(), "settings do not leak between machine registrations");
    std::printf("Machine settings: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
