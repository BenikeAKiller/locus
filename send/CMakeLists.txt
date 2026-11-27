if(APPLE)
    set(LOCUS_SEND_FORMATS VST3 AU)
else()
    set(LOCUS_SEND_FORMATS VST3)
endif()

juce_add_plugin(LocusSend
    PRODUCT_NAME "Locus Send"
    COMPANY_NAME "Locus"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Locu
    PLUGIN_CODE LcSd
    FORMATS ${LOCUS_SEND_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Spatial
    AU_MAIN_TYPE kAudioUnitType_Effect
)

target_sources(LocusSend PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
)

target_compile_definitions(LocusSend PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(LocusSend
    PRIVATE
        locus_core
        locus_ui
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
