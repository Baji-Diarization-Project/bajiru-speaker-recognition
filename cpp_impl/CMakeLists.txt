cmake_minimum_required(VERSION 3.22)
project(LinkjiruPlugin VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Minimal configure path for CI: build only the pure-C++ unit tests (the
# correctness gate) without JUCE, ONNX Runtime, Boost, or the plugin. Everything
# heavy below is gated on this, so a unit-test run needs no GPU, model, or
# submodules. See tests/CMakeLists.txt (LinkjiruUnitTests).
option(LINKJIRU_UNIT_TESTS_ONLY "Configure only the pure-C++ unit tests" OFF)

if(NOT LINKJIRU_UNIT_TESTS_ONLY)

# nlohmann::json
include(FetchContent)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(json)

# We grab Boost via FetchContent using CMake so that we can stay sane.
# We only need a few header-only libraries, so we can avoid the complexity of building Boost ourselves.
set(BOOST_INCLUDE_LIBRARIES beast asio system)
FetchContent_Declare(Boost
    # tbh if anyone know's a better way to get boost via CMake, please tell me, hardcoding the version via URL gives me the heebie-jeebies
    URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(Boost)

# ── ONNX Runtime (DirectML GPU build) ───────────────────────────────────────
# DirectML runs on any DX12 GPU — no CUDA/cuDNN to install or bundle. It is
# distributed as NuGet packages (not a GitHub zip): the ORT DirectML package
# for onnxruntime.dll + headers + import lib, and Microsoft.AI.DirectML for the
# matching DirectML.dll. Both nupkgs are plain zips; DOWNLOAD_NAME forces a
# .zip name so CMake extracts them.
#
# NOTE (verify on first configure): the nupkg-internal paths below are
# best-effort. If configure fails to find onnxruntime.lib / headers /
# DirectML.dll, adjust the paths to match the extracted layout. Keep
# ORT_VERSION and DML_VERSION compatible (see the ORT DirectML release notes).
set(ORT_VERSION "1.20.1")
set(DML_VERSION "1.15.4")

FetchContent_Declare(onnxruntime
    URL https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/${ORT_VERSION}/microsoft.ml.onnxruntime.directml.${ORT_VERSION}.nupkg
    DOWNLOAD_NAME onnxruntime-directml.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(onnxruntime)

FetchContent_Declare(directml
    URL https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/${DML_VERSION}/microsoft.ai.directml.${DML_VERSION}.nupkg
    DOWNLOAD_NAME directml.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(directml)

add_library(onnxruntime_lib SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime_lib PROPERTIES
    IMPORTED_LOCATION "${onnxruntime_SOURCE_DIR}/runtimes/win-x64/native/onnxruntime.dll"
    IMPORTED_IMPLIB "${onnxruntime_SOURCE_DIR}/runtimes/win-x64/native/onnxruntime.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${onnxruntime_SOURCE_DIR}/build/native/include")

# Runtime DLLs to ship next to the plugin — just these two now (no cuDNN).
set(ORT_RUNTIME_DLLS
    "${onnxruntime_SOURCE_DIR}/runtimes/win-x64/native/onnxruntime.dll"
    "${directml_SOURCE_DIR}/bin/x64-win/DirectML.dll")

add_subdirectory(libs/JUCE)

# SharedCode — INTERFACE library holding all plugin sources and dependencies.
# Both the plugin target and the test target link against this so tests compile
# the same code the plugin uses. I.e we save time and maintain sanity
add_library(SharedCode INTERFACE)

file(GLOB LINKJIRU_SOURCES CONFIGURE_DEPENDS src/linkjiru/*.cpp src/linkjiru/*.h src/linkjiru/*.hpp)
target_sources(SharedCode INTERFACE ${LINKJIRU_SOURCES})

target_include_directories(SharedCode
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/linkjiru)

target_link_libraries(SharedCode
    INTERFACE
        juce::juce_audio_utils
        Boost::beast
        nlohmann_json::nlohmann_json
        onnxruntime_lib
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

target_compile_definitions(SharedCode
    INTERFACE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        # VST3-only build: use VST3-native parameter IDs (0), not VST2-compatible
        # ones. JUCE's VST3 client requires this to be defined explicitly.
        JUCE_VST3_CAN_REPLACE_VST2=0
        _WIN32_WINNT=0x0A00
        _CRT_SECURE_NO_WARNINGS
        # ORT/DirectML headers pull in windows.h; keep its min/max macros from
        # clobbering std::min/std::max across the whole project.
        NOMINMAX)

# Plugin target
juce_add_plugin(Linkjiru
    COMPANY_NAME "tomobaji"
    PLUGIN_MANUFACTURER_CODE tmbj
    PLUGIN_CODE 000L
    FORMATS VST3 Standalone
    PRODUCT_NAME "Linkjiru")

target_link_libraries(Linkjiru PRIVATE SharedCode)

# Delay-load onnxruntime.dll so it is not pulled in at plugin-load time, before
# addPluginDirToDllSearch() has told the loader where to find it (and its
# DirectML.dll dependency) next to the plugin. Also ship the ORT DLLs + model
# next to each built binary so it loads during dev runs.
if(MSVC)
    foreach(_fmt Linkjiru_VST3 Linkjiru_Standalone)
        target_link_options(${_fmt} PRIVATE "/DELAYLOAD:onnxruntime.dll")
        target_link_libraries(${_fmt} PRIVATE delayimp.lib) # __delayLoadHelper2
        add_custom_command(TARGET ${_fmt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_RUNTIME_DLLS} "$<TARGET_FILE_DIR:${_fmt}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/onnx-model/runtime_model.onnx" "$<TARGET_FILE_DIR:${_fmt}>"
            COMMENT "Shipping ORT DLLs + model next to ${_fmt}")
    endforeach()
endif()

add_custom_target(BuildAll DEPENDS Linkjiru_VST3 Linkjiru_Standalone LinkjiruUnitTests)

add_custom_command(TARGET Linkjiru_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/artifacts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/Linkjiru_artefacts/$<CONFIG>/VST3/Linkjiru.vst3"
        "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/Linkjiru.vst3"
    COMMENT "Copying Linkjiru.vst3 to artifacts/")

endif()

add_subdirectory(tests)
