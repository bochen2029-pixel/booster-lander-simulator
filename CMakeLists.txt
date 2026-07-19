cmake_minimum_required(VERSION 3.24)

# ============================================================================================
# Booster Lander Simulator — CI-SAFE optional CUDA (M5 MPPI rollout port, rebased onto v3).
#
# CUDA is OPTIONAL and auto-detected so the GitHub CI runner (NO CUDA toolkit) builds a clean
# CPU-only exe, while a CUDA machine builds the GPU --mppi-cuda path. The language is enabled
# CONDITIONALLY (check_language + enable_language), NEVER via project(... CUDA) — declaring CUDA
# in project() hard-fails configure when nvcc is absent, which is exactly the CI breakage we avoid.
#
#   -DBL_CUDA=ON  (default): detect a CUDA toolkit; if found -> GPU path (BL_HAVE_CUDA=1),
#                            else -> WARN and fall back to a clean CPU-only build.
#   -DBL_CUDA=OFF          : force CPU-only (used to prove the CI path on this machine).
# ============================================================================================
project(BoosterLander C)

option(BL_CUDA "Build the optional CUDA MPPI rollout path (auto-detected; CPU-only if absent)" ON)

# Static MSVC runtime -> self-contained exe (house convention, matches Buddhabrot/ORRERY).
# The CUDA host objects must use the SAME runtime; CMAKE_MSVC_RUNTIME_LIBRARY propagates to nvcc's
# -Xcompiler, and static cudart (set in core/ when CUDA is on) keeps the exe dependency-light.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---- Conditional CUDA enable (the CI-safe core) --------------------------------------------
# check_language(CUDA) probes for a usable CUDA compiler WITHOUT failing if none exists; only on
# success do we enable_language(CUDA). BL_CUDA_ENABLED is the single source of truth handed to core/.
set(BL_CUDA_ENABLED OFF)
if(BL_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        set(BL_CUDA_ENABLED ON)
        message(STATUS "BL_CUDA: CUDA toolkit found (${CMAKE_CUDA_COMPILER}) -> building GPU --mppi-cuda path")
    else()
        message(STATUS "BL_CUDA: requested but NO CUDA compiler found -> CPU-only build (--mppi-cuda will error at runtime)")
    endif()
else()
    message(STATUS "BL_CUDA=OFF -> CPU-only build (--mppi-cuda will error at runtime)")
endif()

add_subdirectory(core)

enable_testing()
add_test(NAME selftest COMMAND booster-core --selftest)
