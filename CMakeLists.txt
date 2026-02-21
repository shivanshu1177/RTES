cmake_minimum_required(VERSION 3.20)
project(RTES VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g")

# Find packages
find_package(Threads REQUIRED)

# Include directories
include_directories(include)

# --------------------------------------------------------------------------
# Core library — explicit list so cmake doesn't need re-running on new files
# --------------------------------------------------------------------------
set(CORE_SOURCES
    src/order_book.cpp
    src/error_handling.cpp
    src/config.cpp
    src/risk_manager.cpp
    src/matching_engine.cpp
    src/tcp_gateway.cpp
    src/udp_publisher.cpp
    src/exchange.cpp
    src/strategies.cpp
)

add_library(rtes_core ${CORE_SOURCES})
target_link_libraries(rtes_core Threads::Threads)

# --------------------------------------------------------------------------
# Main exchange executable
# --------------------------------------------------------------------------
add_executable(trading_exchange src/main.cpp)
target_link_libraries(trading_exchange rtes_core)

# --------------------------------------------------------------------------
# Benchmark tools
# --------------------------------------------------------------------------
add_executable(bench_memory_pool tools/bench_memory_pool.cpp)
target_link_libraries(bench_memory_pool rtes_core)

add_executable(bench_matching tools/bench_matching.cpp)
target_link_libraries(bench_matching rtes_core)

add_executable(bench_exchange tools/bench_exchange.cpp)
target_link_libraries(bench_exchange rtes_core)

# --------------------------------------------------------------------------
# Client tools
# --------------------------------------------------------------------------
add_executable(tcp_client tools/tcp_client.cpp)
target_link_libraries(tcp_client rtes_core)

add_executable(client_simulator tools/client_simulator.cpp)
target_link_libraries(client_simulator rtes_core)

add_executable(udp_receiver tools/udp_receiver.cpp)
target_link_libraries(udp_receiver rtes_core)

# --------------------------------------------------------------------------
# Stress test
# --------------------------------------------------------------------------
add_executable(stress_test stress_test.cpp)
target_link_libraries(stress_test rtes_core)

# --------------------------------------------------------------------------
# Unit tests
# --------------------------------------------------------------------------
enable_testing()
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

if(GTest_FOUND OR TARGET gtest)
    file(GLOB_RECURSE TEST_SOURCES "tests/*.cpp")
    add_executable(rtes_tests ${TEST_SOURCES})
    if(TARGET GTest::gtest)
        target_link_libraries(rtes_tests rtes_core GTest::gtest GTest::gtest_main)
    else()
        target_link_libraries(rtes_tests rtes_core gtest gtest_main)
    endif()
    include(GoogleTest)
    gtest_discover_tests(rtes_tests)
endif()
