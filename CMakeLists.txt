# ── CMakeLists.txt ───────────────────────────────────────────
# To regenerate from CMake: cmake -B build
# Then build: cmake --build build

cmake_minimum_required(VERSION 3.20)
project(m1switch
    VERSION 0.1.0
    DESCRIPTION "Nintendo Switch emulator for Apple Silicon"
    LANGUAGES C CXX OBJCXX ASM
)

# ── C++ Standard ──────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

# ── Build types ───────────────────────────────────────────────
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;ASan" CACHE STRING "")

# ── Global compile options ────────────────────────────────────
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
add_compile_options(-fvisibility=hidden)

# macOS Sequoia Clang 21+ has C++ headers in SDK
execute_process(
    COMMAND xcrun --show-sdk-path
    OUTPUT_VARIABLE SDK_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(EXISTS "${SDK_PATH}/usr/include/c++/v1")
    include_directories(BEFORE "${SDK_PATH}/usr/include/c++/v1")
    message(STATUS "Added C++ stdlib headers from SDK: ${SDK_PATH}/usr/include/c++/v1")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
    add_compile_options(-fvisibility-inlines-hidden)
endif()

# ── Apple frameworks ──────────────────────────────────────────
find_library(FOUNDATION_LIB Foundation REQUIRED)
find_library(APPKIT_LIB AppKit REQUIRED)
find_library(METAL_LIB Metal REQUIRED)
find_library(METALKIT_LIB MetalKit REQUIRED)
find_library(CORE_VIDEO_LIB CoreVideo REQUIRED)
find_library(GAME_CONTROLLER_LIB GameController REQUIRED)
find_library(CORE_AUDIO_LIB CoreAudio REQUIRED)
find_library(AUDIO_TOOLBOX_LIB AudioToolbox REQUIRED)
find_library(IOKIT_LIB IOKit REQUIRED)
find_library(CARBON_LIB Carbon REQUIRED)
find_library(UNIFORM_TYPE_ID_LIB UniformTypeIdentifiers REQUIRED)

mark_as_advanced(
    FOUNDATION_LIB APPKIT_LIB METAL_LIB METALKIT_LIB
    CORE_VIDEO_LIB GAME_CONTROLLER_LIB
    CORE_AUDIO_LIB AUDIO_TOOLBOX_LIB IOKIT_LIB CARBON_LIB
)

# ── Third-party dependencies ──────────────────────────────────
option(M1SWITCH_USE_SPIRV "Use SPIRV-Cross + SPIRV-Tools" ON)

if(M1SWITCH_USE_SPIRV)
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/SPIRV-Cross/CMakeLists.txt")
        add_subdirectory(third_party/SPIRV-Cross EXCLUDE_FROM_ALL)
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/SPIRV-Tools/CMakeLists.txt")
        add_subdirectory(third_party/SPIRV-Tools EXCLUDE_FROM_ALL)
    endif()
endif()

# ── Helper to enable ASM on macOS ARM64 ────────────────────
enable_language(ASM)

# ── Static library: m1switch_common ──────────────────────────
file(GLOB_RECURSE COMMON_SRCS src/common/*.cpp src/common/*.mm)
add_library(m1switch_common STATIC ${COMMON_SRCS})
target_include_directories(m1switch_common PUBLIC src)
target_link_libraries(m1switch_common PUBLIC ${FOUNDATION_LIB})

# ── Static library: m1switch_core ────────────────────────────
file(GLOB_RECURSE CORE_SRCS
    src/core/*.cpp src/core/*.mm
    src/cpu/*.cpp src/cpu/*.mm src/cpu/*.S
    src/memory/*.cpp src/memory/*.mm
    src/kernel/*.cpp src/kernel/*.mm
)
add_library(m1switch_core STATIC ${CORE_SRCS})
target_include_directories(m1switch_core PUBLIC src)
target_link_libraries(m1switch_core PUBLIC m1switch_common PRIVATE ${FOUNDATION_LIB})

# ── Static library: m1switch_gpu ─────────────────────────────
file(GLOB_RECURSE GPU_SRCS
    src/gpu/*.cpp src/gpu/*.mm
    src/gpu/maxwell/*.cpp
    src/gpu/shader/*.cpp src/gpu/shader/*.mm src/gpu/shader/ir/*.cpp src/gpu/shader/emit/*.cpp
    src/gpu/backend/*.mm src/gpu/backend/*.cpp
    src/gpu/engine/*.cpp
    src/gpu/texture/*.cpp
)
add_library(m1switch_gpu STATIC ${GPU_SRCS})
target_include_directories(m1switch_gpu PUBLIC src PRIVATE third_party)
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/SPIRV-Cross/spirv_cross.hpp")
    target_include_directories(m1switch_gpu PRIVATE "${CMAKE_SOURCE_DIR}/third_party/SPIRV-Cross")
endif()
target_link_libraries(m1switch_gpu
    PUBLIC m1switch_common
    PUBLIC ${METAL_LIB} ${METALKIT_LIB} ${CORE_VIDEO_LIB}
    PRIVATE ${FOUNDATION_LIB}
)
if(M1SWITCH_USE_SPIRV)
    if(TARGET spirv-cross-core)
        target_link_libraries(m1switch_gpu PUBLIC spirv-cross-core spirv-cross-msl)
        target_compile_definitions(m1switch_gpu PRIVATE M1SWITCH_USE_SPIRV)
    endif()
    if(TARGET SPIRV-Tools-static)
        target_link_libraries(m1switch_gpu PUBLIC SPIRV-Tools-static)
    endif()
    # Add SPIRV-Headers include path from SPIRV-Tools
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/SPIRV-Tools/external/spirv-headers")
        target_include_directories(m1switch_gpu PRIVATE
            "${CMAKE_SOURCE_DIR}/third_party/SPIRV-Tools/external/spirv-headers")
    endif()
endif()

# ── Static library: m1switch_services ────────────────────────
file(GLOB_RECURSE SERVICES_SRCS src/services/*.cpp src/services/*.mm)
add_library(m1switch_services STATIC ${SERVICES_SRCS})
target_include_directories(m1switch_services PUBLIC src)
target_link_libraries(m1switch_services PUBLIC m1switch_common m1switch_core PRIVATE ${FOUNDATION_LIB})

# ── Static library: m1switch_audio ───────────────────────────
file(GLOB_RECURSE AUDIO_SRCS src/audio/*.cpp src/audio/*.mm)
add_library(m1switch_audio STATIC ${AUDIO_SRCS})
target_include_directories(m1switch_audio PUBLIC src)
target_link_libraries(m1switch_audio PUBLIC m1switch_common PRIVATE ${CORE_AUDIO_LIB} ${AUDIO_TOOLBOX_LIB} ${FOUNDATION_LIB})

# ── Static library: m1switch_input ───────────────────────────
file(GLOB_RECURSE INPUT_SRCS src/input/*.cpp src/input/*.mm)
add_library(m1switch_input STATIC ${INPUT_SRCS})
target_include_directories(m1switch_input PUBLIC src)
target_link_libraries(m1switch_input PUBLIC m1switch_common PRIVATE ${GAME_CONTROLLER_LIB} ${IOKIT_LIB} ${CARBON_LIB} ${FOUNDATION_LIB})

# ── Static library: m1switch_loader ──────────────────────────
file(GLOB_RECURSE LOADER_SRCS src/loader/*.cpp src/loader/*.mm)
add_library(m1switch_loader STATIC ${LOADER_SRCS})
target_include_directories(m1switch_loader PUBLIC src)
target_link_libraries(m1switch_loader PUBLIC m1switch_common m1switch_core PRIVATE ${FOUNDATION_LIB})

# ── Static library: m1switch_frontend ────────────────────────
file(GLOB_RECURSE FRONTEND_SRCS src/frontend/*.cpp src/frontend/*.mm)
add_library(m1switch_frontend STATIC ${FRONTEND_SRCS})
target_include_directories(m1switch_frontend PUBLIC src)
target_link_libraries(m1switch_frontend
    PUBLIC m1switch_common m1switch_core m1switch_gpu
    PRIVATE ${APPKIT_LIB} ${METAL_LIB} ${METALKIT_LIB}
            ${CORE_VIDEO_LIB} ${FOUNDATION_LIB} ${UNIFORM_TYPE_ID_LIB}
)

# ── Executable: m1switch ─────────────────────────────────────
file(GLOB_RECURSE MAIN_SRCS src/main.mm)
add_executable(m1switch MACOSX_BUNDLE ${MAIN_SRCS})
target_compile_definitions(m1switch PRIVATE PROJECT_VER="${PROJECT_VERSION}")
target_link_libraries(m1switch
    m1switch_frontend m1switch_gpu m1switch_services
    m1switch_audio m1switch_input m1switch_loader
    m1switch_core m1switch_common
)
set_target_properties(m1switch PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.m1switch.app"
    MACOSX_BUNDLE_BUNDLE_NAME "M1Switch"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE_COPYRIGHT "MIT License"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
    # Hardened Runtime 会阻止 MAP_JIT / mach_vm / SIGTRAP 等核心操作
    # 需要为正式分发添加 com.apple.security.cs.allow-jit 等 entitlement
    # 开发阶段禁用
    #XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES
)

# ── Tests ────────────────────────────────────────────────────
option(M1SWITCH_BUILD_TESTS "Build test targets" ON)
if(M1SWITCH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(src)
    add_subdirectory(tests)
endif()

# ── Headless runner ──────────────────────────────────────────
if(NOT M1SWITCH_BUILD_TESTS)
    add_subdirectory(src)
endif()

# ── Install ──────────────────────────────────────────────────
install(TARGETS m1switch BUNDLE DESTINATION .)
