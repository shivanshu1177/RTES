cmake_minimum_required(VERSION 3.20)
project(RTES VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer")

# Find packages
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Include directories
include_directories(include)

# Core library
file(GLOB_RECURSE CORE_SOURCES "src/*.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
add_library(rtes_core ${CORE_SOURCES})
target_link_libraries(rtes_core Threads::Threads)

# Main executable
add_executable(trading_exchange src/main.cpp)
target_link_libraries(trading_exchange rtes_core)

# Client simulator
add_executable(client_simulator tools/client_simulator.cpp)
target_link_libraries(client_simulator rtes_core)

# Benchmark executables
add_executable(bench_exchange tools/bench_exchange.cpp)
target_link_libraries(bench_exchange rtes_core)

add_executable(bench_memory_pool tools/bench_memory_pool.cpp)
target_link_libraries(bench_memory_pool rtes_core)

add_executable(bench_matching tools/bench_matching.cpp)
target_link_libraries(bench_matching rtes_core)

add_executable(tcp_client tools/tcp_client.cpp)
target_link_libraries(tcp_client rtes_core)

add_executable(udp_receiver tools/udp_receiver.cpp)
target_link_libraries(udp_receiver rtes_core)

add_executable(load_generator tools/load_generator.cpp)
target_link_libraries(load_generator rtes_core)

add_executable(perf_harness tools/perf_harness.cpp)
target_link_libraries(perf_harness rtes_core)

# Enable testing
enable_testing()
find_package(GTest QUIET)
if(GTest_FOUND)
    file(GLOB_RECURSE TEST_SOURCES "tests/*.cpp")
    add_executable(rtes_tests ${TEST_SOURCES})
    target_link_libraries(rtes_tests rtes_core GTest::gtest GTest::gtest_main)
    
    include(GoogleTest)
    gtest_discover_tests(rtes_tests)
endif()

# Install targets
install(TARGETS trading_exchange client_simulator bench_exchange bench_memory_pool bench_matching tcp_client udp_receiver load_generator perf_harness
        RUNTIME DESTINATION bin)
install(DIRECTORY configs/ DESTINATION etc/rtes/)