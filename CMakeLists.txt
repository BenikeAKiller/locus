cmake_minimum_required(VERSION 3.22)

# macOS: universal binary, macOS 12+. Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")

project(Locus VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Windows: link the Visual C++ runtime statically, so the app and plugin run on machines
# without the Visual C++ Redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(JUCE)

add_subdirectory(core)
add_subdirectory(uikit)
add_subdirectory(app)
add_subdirectory(send)

enable_testing()
add_subdirectory(tests)
