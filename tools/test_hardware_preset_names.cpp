#include "controller/HardwarePresetNames.h"
#include "controller/HardwarePresetScan.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

int main() {
    using namespace HardwarePresetNames;
    Names names{}, cache{};
    assert(!strcmp(label(names, 1), "UNKNOWN"));
    cache[0] = "CLEAN";
    cache[1] = "CRUNCH";
    cache[7] = "EIGHT";
    merge(names, true, false, false, cache); // unvalidated files cannot seed names
    assert(names[0].empty());
    merge(names, true, true, true, cache); // first validated identity can seed immediately
    assert(!strcmp(label(names, 1), "CLEAN"));
    assert(!strcmp(label(names, 2), "CRUNCH"));
    assert(!strcmp(label(names, 8), "EIGHT"));
    // Active selection is intentionally not an input. Missing/empty cache
    // entries, including while selecting CRUNCH, cannot erase CLEAN.
    cache.fill("");
    merge(names, true, false, true, cache);
    merge(names, true, false, false, cache);
    assert(names[0] == "CLEAN" && names[1] == "CRUNCH");
    cache[1] = "NEW CRUNCH";
    merge(names, true, false, true, cache);
    assert(names[0] == "CLEAN" && names[1] == "NEW CRUNCH");
    merge(names, true, true, false, cache);
    assert(names[0].empty() && names[1].empty());
    merge(names, true, false, true, cache);
    merge(names, false, false, true, cache);
    for (const auto &name : names) assert(name.empty());
    assert(!strcmp(label(names, 0), "UNKNOWN"));
    assert(!strcmp(label(names, 9), "UNKNOWN"));

    // Simulate the actual transport's refusal to send while a read is pending.
    HardwarePresetScan scan;
    unsigned pending = 0;
    std::vector<unsigned> sends;
    bool available[9]{};
    auto missing = [&](unsigned slot) { return !available[slot]; };
    auto send = [&](unsigned slot) {
        assert(pending == 0); // catches final-timeout wedge in the old scheduler
        pending = slot;
        sends.push_back(slot);
        return true;
    };
    auto cancel = [&]() { pending = 0; };
    auto tick = [&](uint32_t now) { scan.tick(now, 2, missing, send, cancel); };
    tick(0); tick(4999);
    assert(sends.size() == 1);
    tick(5000); tick(10000); tick(15000);
    assert((sends == std::vector<unsigned>{1, 1, 1, 2}));
    available[2] = true;
    tick(15001);
    assert(pending == 0);
    tick(45000);
    assert(sends.size() == 4);
    tick(45001); // a lost slot is revisited without a disconnect or checksum change
    assert(pending == 1 && sends.size() == 5);
    available[1] = true;
    tick(45002);
    tick(75002);
    assert(sends.size() == 5);

    // New link and unsigned millis wrap preserve retry timing.
    scan.reset(); cancel(); available[1] = false;
    tick(0xfffffff0u);
    auto before = sends.size();
    tick(0xfffffff0u + 4999u);
    assert(sends.size() == before);
    tick(0xfffffff0u + 5000u);
    assert(sends.size() == before + 1);

    // A failed send also gets bounded retries, rather than poisoning the scan.
    scan.reset(); cancel();
    unsigned failed = 0;
    auto failSend = [&](unsigned) { ++failed; return false; };
    for (unsigned now = 0; now <= 15000; now += 5000)
        scan.tick(now, 1, missing, failSend, cancel);
    assert(failed == 3);
    scan.tick(45000, 1, missing, failSend, cancel);
    assert(failed == 4);
    std::cout << "hardware preset names/scan: PASS\n";
}
