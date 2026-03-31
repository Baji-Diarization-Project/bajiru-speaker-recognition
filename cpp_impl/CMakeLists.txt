cmake_minimum_required(VERSION 3.22)
project(LinkjiruPlugin VERSION 0.1.0)

add_subdirectory(libs/JUCE)
juce_set_vst2_sdk_path("${CMAKE_CURRENT_SOURCE_DIR}/libs/VST_SDK_2.4")

juce_add_plugin(Linkjiru
    COMPANY_NAME "tomobaji"
    PLUGIN_MANUFACTURER_CODE tmbj
    PLUGIN_CODE 000L
    FORMATS VST VST3 Standalone
    PRODUCT_NAME "Linkjiru")

target_sources(Linkjiru
    PRIVATE
        src/linkjiru/PluginProcessor.cpp
        src/linkjiru/PluginEditor.cpp)

target_compile_definitions(Linkjiru
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Linkjiru
    PRIVATE
        juce::juce_audio_utils
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
