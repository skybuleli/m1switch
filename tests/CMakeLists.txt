# ── Tests ────────────────────────────────────────────────────
# Catch2 will be integrated in a later phase.
# For now, simple standalone test executables.

# ── Integration: NRO loading + SVC patching ──────────────
add_executable(test_nro_loading
    integration/test_nro_loading.cpp
)
target_link_libraries(test_nro_loading
    m1switch_core
    m1switch_loader
    m1switch_common
)
add_test(NAME NroLoading COMMAND test_nro_loading)

# ── E2E: Full boot sequence ─────────────────────────────
add_executable(test_full_boot
    e2e/test_full_boot.cpp
)
target_link_libraries(test_full_boot
    m1switch_core
    m1switch_loader
    m1switch_common
)
add_test(NAME FullBoot COMMAND test_full_boot)

# ── Convenience target to run all tests ──────────────────
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMENT "Running all tests"
)
