#include "sim_config.h"

static SimConfig s_config;

SimConfig &simConfig()
{
    static bool init = false;
    if (!init) {
        s_config.start_time = std::chrono::steady_clock::now();
        init = true;
    }
    return s_config;
}
