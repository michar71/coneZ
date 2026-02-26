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
    std::string c2wasm_path = "c2wasm";
    std::string clang_path = "clang";
    std::string api_header_dir;  // path to conez_api.h directory

    int cone_id = 0;
    int cone_group = 0;
    float origin_lat = 40.7864f;
    float origin_lon = -119.2069f;

    std::string mqtt_broker = "localhost";
    int mqtt_port = 1883;
    bool mqtt_enabled = false;

    bool artnet_enabled = false;
    std::string artnet_host = "255.255.255.255";
    int artnet_port = 6454;
    int artnet_universe = 0;

    // Set at startup
    std::chrono::steady_clock::time_point start_time;
};

SimConfig &simConfig();

#endif
