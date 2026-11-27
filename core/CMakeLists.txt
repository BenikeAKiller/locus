# locus_core: DSP, speaker layouts, parameter definitions, IPC. No GUI code.
# INTERFACE library: its sources compile into each consuming target, alongside
# that target's JUCE modules (see DECISIONS.md).
add_library(locus_core INTERFACE)

target_sources(locus_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/LocusCore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/layouts/SpeakerLayout.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/Panner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/VbapPanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/KnnPanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/HoaPanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/LayoutState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/SourceProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/LateReverb.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/RenderEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/params/Parameters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ipc/SharedMemory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ipc/SendEndpoint.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ipc/AppEndpoint.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/session/SessionFile.cpp
)

target_include_directories(locus_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(locus_core INTERFACE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_dsp
)

target_compile_definitions(locus_core INTERFACE
    LOCUS_VERSION_STRING="${PROJECT_VERSION}"
)
