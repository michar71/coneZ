cmake_minimum_required(VERSION 3.16)
project(conez-simulator LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ---- Version / build number ----
set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)
set(BUILDNUM_FILE "${CMAKE_SOURCE_DIR}/../../tools/buildnum.txt")
if(EXISTS "${BUILDNUM_FILE}")
    file(READ "${BUILDNUM_FILE}" _BUILDNUM)
    string(STRIP "${_BUILDNUM}" _BUILDNUM)
else()
    set(_BUILDNUM 0)
endif()
math(EXPR BUILD_NUMBER "${_BUILDNUM} + 1")

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# ---- wasm3 static library (pure C) ----
file(GLOB WASM3_SOURCES thirdparty/wasm3/source/*.c)
add_library(m3 STATIC ${WASM3_SOURCES})
target_include_directories(m3 PUBLIC thirdparty/wasm3/source)
target_compile_definitions(m3 PRIVATE
    d_m3HasWASI=0
    d_m3LogOutput=0
)
# Suppress warnings in vendored code
target_compile_options(m3 PRIVATE -w)

# ---- simulator executable ----
set(SIM_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/gui/led_strip_widget.cpp
    src/gui/console_widget.cpp
    src/gui/sensor_panel.cpp
    src/state/led_state.cpp
    src/state/sensor_state.cpp
    src/state/sim_config.cpp
    src/state/cue_engine.cpp
    src/wasm/sim_wasm_runtime.cpp
    src/wasm/sim_wasm_imports_led.cpp
    src/wasm/sim_wasm_imports_sensors.cpp
    src/wasm/sim_wasm_imports_datetime.cpp
    src/wasm/sim_wasm_imports_system.cpp
    src/wasm/sim_wasm_imports_io.cpp
    src/wasm/sim_wasm_imports_file.cpp
    src/wasm/sim_wasm_imports_math.cpp
    src/wasm/sim_wasm_imports_string.cpp
    src/wasm/sim_wasm_imports_format.cpp
    src/wasm/sim_wasm_imports_gpio.cpp
    src/worker/wasm_worker.cpp
    src/worker/compiler_worker.cpp
)

add_executable(conez-simulator ${SIM_SOURCES})

target_include_directories(conez-simulator PRIVATE
    src
    src/gui
    src/state
    src/wasm
    src/worker
    thirdparty/wasm3/source
)

target_compile_definitions(conez-simulator PRIVATE
    VERSION_MAJOR=${VERSION_MAJOR}
    VERSION_MINOR=${VERSION_MINOR}
    BUILD_NUMBER=${BUILD_NUMBER}
)

target_link_libraries(conez-simulator PRIVATE Qt6::Widgets m3 m)

# Increment build number after successful build
add_custom_command(TARGET conez-simulator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "${BUILD_NUMBER}" > "${BUILDNUM_FILE}"
    COMMENT "Build number: ${BUILD_NUMBER}"
)
