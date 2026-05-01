// RawAccel Linux — libFuzzer harness for config JSON parsing
//
// Exercises: json::parse → device_profile_from_json → sanitize_device_profile
// and:       json::parse → load_config path (via raw JSON string)
//
// Build:
//   clang++ -std=c++20 -O2 -g -fsanitize=fuzzer,address,undefined \
//       -I include tests/fuzz_config.cpp src/config.cpp           \
//       -o build-manual/fuzz_config
//
// Run:
//   mkdir -p tests/corpus_config
//   ./build-manual/fuzz_config tests/corpus_config -max_len=8192 -timeout=5
//
// Seed corpus is optional; the fuzzer will discover interesting inputs on its own.
// To add seed inputs, place JSON files in tests/corpus_config/.

#include "../include/config.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <fstream>
#include <filesystem>

using namespace rawaccel;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Cap input to avoid excessive memory usage from deeply nested JSON.
    if (size > 8192) return 0;

    std::string input(reinterpret_cast<const char*>(data), size);

    // ── Path 1: profile_from_json (single profile) ──────────────────────────
    try {
        device_profile dp = profile_from_json(input);
        // If parsing succeeds, verify sanitization invariants.
        // These should never fire thanks to sanitize_device_profile().
        if (dp.dev_cfg.dpi < 1 || dp.dev_cfg.dpi > 32000) __builtin_trap();
        if (dp.dev_cfg.polling_rate < 125 || dp.dev_cfg.polling_rate > 8000) __builtin_trap();
        if (dp.prof.degrees_rotation < 0 || dp.prof.degrees_rotation >= 360) __builtin_trap();
        if (dp.prof.degrees_snap < 0 || dp.prof.degrees_snap > 45) __builtin_trap();
    } catch (...) {
        // Parse errors are expected for random input — not a bug.
    }

    // ── Path 2: load_config (full config file round-trip) ───────────────────
    try {
        // Write fuzz input to a temp file and load it.
        std::string tmp = "/tmp/.rawaccel_fuzz_" +
                          std::to_string(reinterpret_cast<uintptr_t>(data)) + ".json";
        {
            std::ofstream f(tmp, std::ios::binary);
            f.write(input.c_str(), input.size());
        }
        app_config cfg = load_config(tmp);
        std::filesystem::remove(tmp);

        // Verify sanitization invariants on every loaded profile.
        for (auto& dp : cfg.profiles) {
            if (dp.dev_cfg.dpi < 1 || dp.dev_cfg.dpi > 32000) __builtin_trap();
            if (dp.dev_cfg.polling_rate < 125 || dp.dev_cfg.polling_rate > 8000) __builtin_trap();
        }
    } catch (...) {
        // Expected for malformed JSON.
    }

    return 0;
}
