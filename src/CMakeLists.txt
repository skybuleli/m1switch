# ── Headless CLI (no GUI) ────────────────────────────────────
add_executable(m1switch_headless
    ${CMAKE_CURRENT_SOURCE_DIR}/headless_main.cpp
)
target_link_libraries(m1switch_headless
    m1switch_core m1switch_loader m1switch_gpu
    m1switch_services m1switch_common
)
