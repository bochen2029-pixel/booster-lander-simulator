# core/ — C11 plant + guidance, with an OPTIONAL CUDA MPPI rollout (M5, rebased onto protocol v3).
#
# Two source layouts, selected by BL_CUDA_ENABLED (resolved in the top-level CMakeLists):
#
#  * CUDA ON  — guidance_mppi_cuda.cu UNITY-#includes the plant physics (atmosphere/dynamics/
#    integrator/contact/control .c) so nvcc emits __host__ __device__ code from BYTE-IDENTICAL
#    source (directive 7). Those .c are therefore compiled by the .cu, NOT by the C target. The
#    remaining .c (driver + guidance) stay C and link against the .cu object. BL_HAVE_CUDA=1.
#
#  * CUDA OFF (CI runner / -DBL_CUDA=OFF) — no .cu; the C target compiles ALL .c (plant included);
#    BL_HAVE_CUDA is undefined so main.c/sim.c compile the CPU-only path (--mppi-cuda errors at the
#    CLI). The BL_HD qualifiers in the plant .c expand to nothing under a non-nvcc compiler, so the
#    CPU exe is byte-identical to plain v3 main.

# ---- C translation units common to BOTH builds (driver + guidance; NOT the plant physics) ----
set(BL_C_COMMON
    main.c
    scenario.c
    sim.c
    sea.c               # SEA module (§4.4): deterministic P-M droneship deck heave (D-035; pure math, host-side)
    nav.c
    ws.c
    guidance_hoverslam.c
    guidance_mppi.c
    guidance_neural.c   # N1 §9.8: tier-3 GM_NEURAL inference (fixed-order fp64; ships dead behind --neural)
)
# ---- plant physics: compiled by the C target ONLY when CUDA is off (else the .cu owns them) ----
set(BL_C_PLANT
    atmosphere.c
    dynamics.c
    integrator.c
    contact.c
    control.c
)

if(BL_CUDA_ENABLED)
    add_executable(booster-core
        ${BL_C_COMMON}
        guidance_mppi_cuda.cu   # CUDA TU: host+device plant physics (unity #include of the .c) + MPPI kernels
    )
    target_compile_definitions(booster-core PRIVATE _CRT_SECURE_NO_WARNINGS BL_HAVE_CUDA=1)

    # sm_89 (RTX 40-series), CUDA 13.1. Determinism: -fmad=false, NO --use_fast_math (house rule).
    set(CMAKE_CUDA_RUNTIME_LIBRARY Static)   # static cudart -> dependency-light exe
    set_target_properties(booster-core PROPERTIES
        CUDA_ARCHITECTURES 89
        CUDA_STANDARD 14
        CUDA_SEPARABLE_COMPILATION OFF
    )
    # per-language flags: generator expressions keep C and CUDA flags from cross-contaminating.
    target_compile_options(booster-core PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:-fmad=false;-diag-suppress=550;-Xcudafe;--diag_suppress=550>
    )
    if(MSVC)
        target_compile_options(booster-core PRIVATE $<$<COMPILE_LANGUAGE:C>:/fp:precise;/W3>)
    else()
        target_compile_options(booster-core PRIVATE $<$<COMPILE_LANGUAGE:C>:-O2;-ffp-contract=off;-Wall>)
    endif()
    message(STATUS "core: CUDA MPPI rollout ENABLED (sm_89, -fmad=false); plant physics compiled via the .cu")
else()
    # CPU-only: compile everything (plant included) as C; no .cu, no BL_HAVE_CUDA.
    add_executable(booster-core
        ${BL_C_COMMON}
        ${BL_C_PLANT}
    )
    target_compile_definitions(booster-core PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(MSVC)
        target_compile_options(booster-core PRIVATE /fp:precise /W3)
    else()
        target_compile_options(booster-core PRIVATE -O2 -ffp-contract=off -Wall)
        target_link_libraries(booster-core PRIVATE m)
    endif()
    message(STATUS "core: CPU-only build (no CUDA); --mppi-cuda will error at runtime")
endif()

target_include_directories(booster-core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(booster-core PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# --serve telemetry socket (Win32 Winsock2). Additive; headless path unaffected.
if(WIN32)
    target_link_libraries(booster-core PRIVATE ws2_32)
endif()

# OpenMP: parallelize the CPU MPPI K-rollout loop (guidance_mppi.c). Deterministic (independent
# rollouts + fixed pairwise weight reduction). Optional (serial fallback). C language only.
find_package(OpenMP QUIET)
if(OpenMP_C_FOUND)
    target_link_libraries(booster-core PRIVATE OpenMP::OpenMP_C)
    message(STATUS "OpenMP enabled for CPU MPPI rollouts")
endif()
