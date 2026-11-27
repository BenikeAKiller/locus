juce_add_console_app(LocusTests
    PRODUCT_NAME "LocusTests"
)

target_sources(LocusTests PRIVATE
    Main.cpp
    CoreTests.cpp
    LayoutTests.cpp
    PannerTests.cpp
    DspTests.cpp
    EngineTests.cpp
    IpcTests.cpp
    UiTests.cpp
    SessionTests.cpp
    # The Send plugin's processor and editor, for the editor layout test.
    ${PROJECT_SOURCE_DIR}/send/PluginProcessor.cpp
    ${PROJECT_SOURCE_DIR}/send/PluginEditor.cpp
)

target_compile_definitions(LocusTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="Locus Send"
)

target_link_libraries(LocusTests
    PRIVATE
        locus_core
        locus_app_lib
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_test(NAME LocusTests COMMAND LocusTests)

# End-to-end check of the built Send plugin, hosted like a DAW would (not run by ctest, because
# it uses the real shared memory and must not run while the Locus app is open).
juce_add_console_app(LocusPluginCheck
    PRODUCT_NAME "LocusPluginCheck"
)

target_sources(LocusPluginCheck PRIVATE PluginCheck.cpp)

target_compile_definitions(LocusPluginCheck PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
)

target_link_libraries(LocusPluginCheck
    PRIVATE
        locus_core
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
