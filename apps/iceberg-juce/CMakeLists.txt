cmake_minimum_required(VERSION 3.20)

# JUCE-based standalone GUI for the Iceberg auralisation library.
# Built only when the top-level option ICEBERG_BUILD_GUI is ON.

juce_add_gui_app(iceberg-juce
    PRODUCT_NAME "Iceberg"
    BUNDLE_ID "org.aguirre.iceberg"
    COMPANY_NAME "Aguirre"
    VERSION "${PROJECT_VERSION}"
    DOCUMENT_EXTENSIONS "iceberg-exp"
)

target_sources(iceberg-juce PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/Theme.cpp
    Source/components/SourcePositionPad.cpp
    Source/components/SlotListPanel.cpp
    Source/components/SlotPropertiesPanel.cpp
    Source/components/StatusBar.cpp
    Source/render/RenderOrchestrator.cpp
    Source/audio/AudioPlayer.cpp
    Source/audio/SignalFile.cpp
    Source/audio/IcebergAudioSource.cpp
    Source/audio/LiveAudioSource.cpp
    Source/audio/StereoFold.cpp
    Source/room_calibration/DevicePlayRecord.cpp
    Source/room_calibration/CalibrationComponent.cpp
    Source/room_calibration/ResponsePlot.cpp
)

target_include_directories(iceberg-juce PRIVATE
    Source
)

target_compile_definitions(iceberg-juce PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:iceberg-juce,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:iceberg-juce,JUCE_VERSION>"
)

target_link_libraries(iceberg-juce PRIVATE
    juce::juce_gui_extra
    juce::juce_audio_utils
    iceberg_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
