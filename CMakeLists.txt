cmake_minimum_required(VERSION 3.24)
project(BoosterLander C)

# Static MSVC runtime -> self-contained exe (house convention, matches Buddhabrot/ORRERY)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_subdirectory(core)

enable_testing()
add_test(NAME selftest COMMAND booster-core --selftest)
