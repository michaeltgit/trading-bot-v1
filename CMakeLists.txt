cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
project(TradingEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(TRADING_NATIVE_ARCH "Build Release with -march=native" ON)

# -----------------------------------------------------------------------------
# Compile flags
# -----------------------------------------------------------------------------
add_library(trading_flags INTERFACE)
target_compile_options(trading_flags INTERFACE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wnon-virtual-dtor
    -Wcast-align -Woverloaded-virtual -Wdouble-promotion
    -Wno-unused-parameter
    $<$<CONFIG:Debug>:-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined>
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
    $<$<AND:$<CONFIG:Release>,$<BOOL:${TRADING_NATIVE_ARCH}>>:-march=native>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g -DNDEBUG>
)
target_link_options(trading_flags INTERFACE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------
find_package(Boost 1.70 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${Boost_INCLUDE_DIRS}
    ${nlohmann_json_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
)
FetchContent_Declare(
    simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v3.9.3
)
FetchContent_MakeAvailable(Catch2 simdjson)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

# -----------------------------------------------------------------------------
# trading_core
# -----------------------------------------------------------------------------
add_library(trading_core
    src/core/logger.cpp
    src/core/metrics.cpp
    src/core/runtime.cpp
    src/md/coinbase_auth.cpp
    src/md/coinbase_parser.cpp
    src/md/coinbase_ws_client.cpp
    src/strategy/imbalance_strategy.cpp
)

target_link_libraries(trading_core
    PUBLIC
        trading_flags
        nlohmann_json::nlohmann_json
        simdjson
    PRIVATE
        Boost::headers
        OpenSSL::SSL
        OpenSSL::Crypto
)

# -----------------------------------------------------------------------------
# trading_engine executable
# -----------------------------------------------------------------------------
add_executable(trading_engine src/main.cpp)
target_link_libraries(trading_engine PRIVATE trading_core)

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
enable_testing()
add_executable(tests_cpp
    # core / util
    tests/test_types.cpp
    tests/test_result.cpp
    tests/test_logger.cpp
    tests/test_spsc_queue.cpp
    tests/test_hdr_histogram.cpp
    tests/test_bounded_book.cpp

    # md
    tests/test_coinbase_parser.cpp
    tests/test_coinbase_auth.cpp

    # strategy
    tests/test_order_sizer.cpp
    tests/test_pnl_tracker.cpp
    tests/test_imbalance_strategy.cpp

    # exec
    tests/test_risk_manager.cpp
    tests/test_execution_engine.cpp
    tests/test_circuit_breaker.cpp

    # replay + benchmarks
    tests/test_replay.cpp
    tests/test_benchmarks.cpp
)
target_link_libraries(tests_cpp PRIVATE trading_core Catch2::Catch2WithMain)
target_compile_definitions(tests_cpp PRIVATE
    TRADING_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
catch_discover_tests(tests_cpp)
