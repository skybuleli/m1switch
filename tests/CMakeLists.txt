# ── Tests ────────────────────────────────────────────────────

# ── Integration: NRO loading + SVC patching ──────────────
add_executable(test_nro_loading
    integration/test_nro_loading.cpp
)
target_link_libraries(test_nro_loading
    m1switch_core m1switch_loader m1switch_common
)
add_test(NAME NroLoading COMMAND test_nro_loading)

# ── E2E: Full boot sequence ─────────────────────────────
add_executable(test_full_boot
    e2e/test_full_boot.cpp
)
target_link_libraries(test_full_boot
    m1switch_core m1switch_loader m1switch_common
)
add_test(NAME FullBoot COMMAND test_full_boot)

# ── Unit: GPU parser (Phase 2) ──────────────────────────
add_executable(test_gpu_parser
    unit/test_gpu_parser.cpp
)
target_link_libraries(test_gpu_parser
    m1switch_gpu m1switch_common
)
add_test(NAME GpuParser COMMAND test_gpu_parser)

# ── Convenience target ──────────────────────────────────
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMENT "Running all tests"
)
