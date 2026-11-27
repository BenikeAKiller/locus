# Shared UI kit for the app and the Send plugin: theme, fonts, look-and-feel, controls and the
# layout checker (spec §9.1).
juce_add_binary_data(locus_fonts
    HEADER_NAME LocusFonts.h
    NAMESPACE LocusFonts
    SOURCES
        ${PROJECT_SOURCE_DIR}/resources/fonts/Inter-Regular.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/Inter-SemiBold.ttf
)

add_library(locus_ui INTERFACE)

target_sources(locus_ui INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Formatting.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LocusLookAndFeel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Knob.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ToggleDot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LayoutChecker.cpp
)

target_include_directories(locus_ui INTERFACE ${PROJECT_SOURCE_DIR})

target_link_libraries(locus_ui INTERFACE
    locus_core
    locus_fonts
    juce::juce_gui_basics
)
