cmake_minimum_required(VERSION 3.22)
project(LinkjiruPlugin VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

add_subdirectory(libs/JUCE)
juce_set_vst2_sdk_path("${CMAKE_CURRENT_SOURCE_DIR}/libs/VST_SDK_2.4")

juce_add_plugin(Linkjiru
    COMPANY_NAME "tomobaji"
    PLUGIN_MANUFACTURER_CODE tmbj
    PLUGIN_CODE 000L
    FORMATS VST VST3 Standalone
    PRODUCT_NAME "Linkjiru")

file(GLOB LINKJIRU_SOURCES src/linkjiru/*.cpp src/linkjiru/*.h)
target_sources(Linkjiru PRIVATE ${LINKJIRU_SOURCES})

target_compile_definitions(Linkjiru
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        _WIN32_WINNT=0x0A00
        _CRT_SECURE_NO_WARNINGS)

target_link_libraries(Linkjiru
    PRIVATE
        juce::juce_audio_utils
        Boost::beast
        nlohmann_json::nlohmann_json
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

add_custom_target(BuildAll DEPENDS Linkjiru_VST Linkjiru_VST3 Linkjiru_Standalone)

add_custom_command(TARGET Linkjiru_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/artifacts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/Linkjiru_artefacts/$<CONFIG>/VST3/Linkjiru.vst3"
        "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/Linkjiru.vst3"
    COMMENT "Copying Linkjiru.vst3 to artifacts/")

add_custom_command(TARGET Linkjiru_VST POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/artifacts"
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE:Linkjiru_VST>"
        "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/Linkjiru.dll"
    COMMENT "Copying Linkjiru.dll to artifacts/")
