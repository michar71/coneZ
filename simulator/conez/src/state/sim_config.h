#ifndef SIM_CONFIG_H
#define SIM_CONFIG_H

#include <string>
#include <chrono>

struct SimConfig {
    int led_count1 = 50;
    int led_count2 = 50;
    int led_count3 = 50;
    int led_count4 = 50;

    std::string sandbox_path = "/tmp/conez_sandbox";
    std::string bas2wasm_path = "bas2wasm";
    std::string clang_path = "clang";
    std::string api_header_dir;  // path to conez_api.h directory

    // Set at startup
    std::chrono::steady_clock::time_point start_time;
};

SimConfig &simConfig();

#endif
