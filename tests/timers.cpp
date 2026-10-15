// Regression tests for cyclic timers that fall behind the emulated clock.
#include <core/timermanager.h>

#include <cstdio>
#include <vector>

int main()
{
    auto tm = TimerManager::get_instance();
    uint64_t now = 0;
    tm->set_time_now_cb([&]() { return now; });
    tm->set_notify_changes_cb([]() {});

    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    };

    // Model the separate beginning/end timers for a video blanking pulse.
    bool blanking = false;
    std::vector<int> edges;
    auto start = tm->add_cyclic_timer(100, [&]() {
        blanking = true;
        edges.push_back(1);
    });
    auto end = tm->add_cyclic_timer(100, 110, [&]() {
        blanking = false;
        edges.push_back(0);
    });

    // Both timers missed several deadlines. Deliver each only once, then
    // retain their original phase so the guest can observe the next pulse.
    now = 350;
    check(tm->process_timers() == 50, "next frame stays on its original deadline");
    check(edges == std::vector<int>({1, 0}), "missed frames do not cause a callback burst");
    now = 400;
    check(tm->process_timers() == 10, "blanking end stays ten ticks after its start");
    check(blanking, "guest can observe blanking after a delay");
    now = 410;
    check(tm->process_timers() == 90, "next frame retains its cadence");
    check(!blanking, "blanking ends at its own deadline");

    // Landing exactly on a later deadline must schedule strictly in the future.
    now = 1000;
    check(tm->process_timers() == 10, "exact missed deadline preserves the other timer's phase");
    now = 1010;
    check(tm->process_timers() == 90, "exact deadline does not repeatedly fire a timer");
    tm->cancel_timer(start);
    tm->cancel_timer(end);
    check(tm->process_timers() == 0, "cyclic timers remain cancellable");

    int calls = 0;
    tm->add_oneshot_timer(20, [&]() { ++calls; });
    now += 100;
    check(tm->process_timers() == 0 && calls == 1, "overdue one-shot fires once");
    tm->process_timers();
    check(calls == 1, "one-shot is not rearmed");

    std::printf("Timer tests: %d failures\n", failures);
    return failures ? 1 : 0;
}
