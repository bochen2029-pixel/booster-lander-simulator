# core/ — C11, zero external deps. CUDA added at M4/M5 as an optional language.
add_executable(booster-core
    main.c
    atmosphere.c
    dynamics.c
    integrator.c
    contact.c
    control.c
    guidance_hoverslam.c
    scenario.c
    sim.c
)

target_include_directories(booster-core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(booster-core PRIVATE _CRT_SECURE_NO_WARNINGS)

if(MSVC)
    target_compile_options(booster-core PRIVATE /fp:precise /W3)
    # /fp:precise + no fast contraction: determinism over last-percent speed (house rule)
else()
    target_compile_options(booster-core PRIVATE -O2 -ffp-contract=off -Wall)
    target_link_libraries(booster-core PRIVATE m)
endif()

set_target_properties(booster-core PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
