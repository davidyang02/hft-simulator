cmake_minimum_required(VERSION 3.16)
project(hft-simulator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -O3)
endif()

# Fetch FTXUI for terminal UI
include(FetchContent)
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

# Fetch nlohmann/json for config file parsing
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Main library
add_library(hft_core
    src/order_book.cpp
    src/matching_engine.cpp
)

# Main executable (console mode)
add_executable(hft-sim src/main.cpp)
target_link_libraries(hft-sim hft_core)

# Terminal UI executable
add_executable(hft-ui src/ui_main.cpp)
target_link_libraries(hft-ui 
    hft_core 
    ftxui::screen 
    ftxui::dom 
    ftxui::component
    nlohmann_json::nlohmann_json
)

# Tests
enable_testing()

add_executable(test_order_book test/test_order_book.cpp)
target_link_libraries(test_order_book hft_core)
add_test(NAME OrderBookTests COMMAND test_order_book)

add_executable(test_matching_engine test/test_matching_engine.cpp)
target_link_libraries(test_matching_engine hft_core)
add_test(NAME MatchingEngineTests COMMAND test_matching_engine)

# Benchmark
add_executable(bench_matching benchmark/bench_matching.cpp)
target_link_libraries(bench_matching hft_core)
