# Everything except main() is a library, so the UI tests can build the real main window.
add_library(locus_app_lib INTERFACE)

target_sources(locus_app_lib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/AudioHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/model/AppController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/model/Bindings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/CanvasView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/Chrome.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/MainComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/SpeakerEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/Tabs.cpp
)

target_include_directories(locus_app_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(locus_app_lib INTERFACE
    locus_core
    locus_ui
    juce::juce_gui_basics
    juce::juce_audio_utils
)

juce_add_gui_app(Locus
    PRODUCT_NAME "Locus"
    COMPANY_NAME "Locus"
    VERSION ${PROJECT_VERSION}
)

target_sources(Locus PRIVATE Main.cpp)

target_compile_definitions(Locus PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Locus,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Locus,JUCE_VERSION>"
)

target_link_libraries(Locus
    PRIVATE
        locus_app_lib
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
