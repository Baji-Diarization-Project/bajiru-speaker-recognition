include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Unit tests: pure C++ over the header-only logic (ring buffer, gating, cadence,
# resampler). No JUCE, ONNX Runtime, GPU, or model, so it builds and runs on any
# machine and is the whole test suite. Uses GTest's own main().
add_executable(LinkjiruUnitTests
    test_shared_ring_buffer.cpp
    test_gating_logic.cpp
    test_cadence_gate.cpp
    test_resampler_quality.cpp)

target_include_directories(LinkjiruUnitTests
    PRIVATE ${CMAKE_SOURCE_DIR}/src/linkjiru)

target_compile_definitions(LinkjiruUnitTests
    PRIVATE
        _WIN32_WINNT=0x0A00
        _CRT_SECURE_NO_WARNINGS)

target_link_libraries(LinkjiruUnitTests
    PRIVATE GTest::gtest_main)
