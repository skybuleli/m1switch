# ── Tests ────────────────────────────────────────────────────

# ── All-in-one test suite ───────────────────────────────
add_executable(all_tests
    all_tests.cpp
    test_framework.h
)
target_include_directories(all_tests PRIVATE ..)
target_link_libraries(all_tests
    m1switch_core m1switch_loader m1switch_services
    m1switch_gpu m1switch_common
)
add_test(NAME AllTests COMMAND all_tests)

# ── Legacy tests (keep for now) ─────────────────────────
add_executable(test_nro_loading
    integration/test_nro_loading.cpp
)
target_link_libraries(test_nro_loading
    m1switch_core m1switch_loader m1switch_common
)
add_test(NAME NroLoading COMMAND test_nro_loading)

add_executable(test_gpu_parser
    unit/test_gpu_parser.cpp
)
target_link_libraries(test_gpu_parser
    m1switch_gpu m1switch_common
)
add_test(NAME GpuParser COMMAND test_gpu_parser)

# ── Headless NRO runner (disabled for now) ──────────────

# ── Convenience target ──────────────────────────────────
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMENT "Running all tests"
)
