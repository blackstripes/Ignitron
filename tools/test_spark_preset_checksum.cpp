#include "SparkPresetChecksum.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

std::vector<uint8_t> hex(const std::string &s) {
    assert(s.size() % 2 == 0);
    std::vector<uint8_t> result;
    for (size_t i = 0; i < s.size(); i += 2)
        result.push_back(static_cast<uint8_t>(std::stoul(s.substr(i, 2), nullptr, 16)));
    return result;
}

int main() {
    // Actual decoded 03/01 slot-1 response from NEO Core, 2026-09-28.
    // Seven-pedal body ends at byte 389. The old parser read byte 389 (CA)
    // as the checksum, rejecting CLEAN against the hardware checksum 9A.
    const auto clean = hex(
        "0000d92432323135444336332d313244322d344343422d414534392d393733354543343639423330"
        "a6434c45414e20a3302e37a0a869636f6e2e706e67ca42f0000097"
        "ae626961732e6e6f69736567617465c3930091ca3d95eec30191ca3f662cde0291ca3f800000"
        "a8426c7565436f6d70c3940091ca3e696da50191ca3f2c9adb0291ca3e764e770391ca3f3f8739"
        "a7426f6f73746572c3910091ca3ecf1847"
        "ab39344d6174636844435632c3950091ca3f391fbc0191ca3f1d182e0291ca3ed4c77b0391ca3ee07e080491ca3f363fba"
        "a9477569746172455136c3970091ca3f0000000191ca3f0000000291ca3eee58480391ca3f0f3f660491ca3f1745d10591ca3f14dd7e0691ca3f000000"
        "aa44656c61795265323031c2950091ca3e63e7160191ca3e4e98170291ca3e87556c0391ca3eb9551a0491ca3f800000"
        "ab626961732e726576657262c3980091ca3f3d6c900191ca3f695c160291ca3f0cadca0391ca3f4eade60491ca3f1442d00591ca3e90890e0691ca3e99999a0791ca3f800000"
        "cac1e80000ca3f0000009a");
    uint8_t checksum = 0;
    assert(clean.size() == 400);
    assert(clean[389] == 0xCA);
    assert(readSparkPresetChecksum(clean.data(), clean.size(), 389, checksum));
    assert(checksum == 0x9A);

    // Address bytes are excluded. The same content can be returned as current
    // or hardware, and checksum validation does NOT establish slot identity.
    auto current = clean;
    current[0] = 1;
    current[1] = 7;
    assert(readSparkPresetChecksum(current.data(), current.size(), 389, checksum));
    assert(checksum == 0x9A); // separate outstanding-slot guard remains required

    // Legacy shape: same body, no extension, checksum of the body is 1E.
    auto legacy = clean;
    legacy.resize(390);
    legacy.back() = 0x1E;
    assert(readSparkPresetChecksum(legacy.data(), legacy.size(), 389, checksum));
    assert(checksum == 0x1E);

    // Every truncation, wrong parse position, corrupt content and unknown tail
    // must fail without changing the caller's checksum output.
    for (size_t length = 0; length < clean.size(); ++length) {
        checksum = 0x55;
        assert(!readSparkPresetChecksum(clean.data(), length, 389, checksum));
        assert(checksum == 0x55);
    }
    assert(!readSparkPresetChecksum(nullptr, 400, 389, checksum));
    assert(!readSparkPresetChecksum(clean.data(), clean.size(), size_t(-1), checksum));
    assert(!readSparkPresetChecksum(clean.data(), clean.size(), 390, checksum));
    auto bad = clean;
    bad[100] ^= 1;
    assert(!readSparkPresetChecksum(bad.data(), bad.size(), 389, checksum));
    bad = clean;
    bad.back() ^= 1;
    assert(!readSparkPresetChecksum(bad.data(), bad.size(), 389, checksum));
    // Keep the byte sum identical while corrupting the extension's type marker.
    bad = clean;
    --bad[389]; ++bad[390];
    assert(!readSparkPresetChecksum(bad.data(), bad.size(), 389, checksum));
    bad = clean;
    --bad[394]; ++bad[395];
    assert(!readSparkPresetChecksum(bad.data(), bad.size(), 389, checksum));
    bad = clean;
    bad.insert(bad.end(), clean.begin(), clean.end());
    assert(!readSparkPresetChecksum(bad.data(), bad.size(), 389, checksum));
    std::cout << "Spark preset checksum/tail: PASS (captured NEO CLEAN + legacy + malformed)\n";
}
