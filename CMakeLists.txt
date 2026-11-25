cmake_minimum_required(VERSION 3.28)

project(HCSUDA
    VERSION 0.1.0
    DESCRIPTION "HCS Unified Compute Runtime"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows.h must never define min/max macros; they break <algorithm> in C++.
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

option(HCS_ENABLE_VULKAN "Build the Vulkan compute backend" ON)
option(HCS_BUILD_TESTS "Build the test suite" ON)
option(HCS_BUILD_TOOLS "Build command line tools" ON)
option(HCS_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(HCS_VALIDATION_LAYERS "Enable Vulkan validation layers in tests" OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Vulkan SDK discovery (build-time only; never required at runtime beyond the
# loader and vendor ICD that the GPU driver installs).
# ---------------------------------------------------------------------------
if(HCS_ENABLE_VULKAN)
    find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h
        HINTS "$ENV{VULKAN_SDK}/Include" "$ENV{VULKAN_SDK}/include")
    find_program(GLSLC_EXECUTABLE glslc
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
    if(NOT VULKAN_INCLUDE_DIR OR NOT GLSLC_EXECUTABLE)
        message(WARNING
            "Vulkan headers or glslc not found. Configuring a CPU-only build. "
            "Install the Vulkan SDK to enable the Vulkan backend.")
        set(HCS_ENABLE_VULKAN OFF)
    endif()
endif()

# ---------------------------------------------------------------------------
# Shader compilation and embedding
# ---------------------------------------------------------------------------
set(HCS_SHADER_GENERATED)
if(HCS_ENABLE_VULKAN)
    set(HCS_SPV_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${HCS_SPV_DIR}")
    set(HCS_SHADERS vecadd scale reduce_sum matmul)
    foreach(shader IN LISTS HCS_SHADERS)
        set(src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/hksl/${shader}.comp")
        set(spv "${HCS_SPV_DIR}/${shader}.spv")
        set(gen "${HCS_SPV_DIR}/spv_${shader}.cpp")
        add_custom_command(
            OUTPUT "${spv}"
            COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.1 -O "${src}" -o "${spv}"
            DEPENDS "${src}"
            COMMENT "glslc ${shader}.comp -> ${shader}.spv"
            VERBATIM)
        add_custom_command(
            OUTPUT "${gen}"
            COMMAND "${CMAKE_COMMAND}" "-DINPUT=${spv}" "-DOUTPUT=${gen}"
                    "-DNAME=${shader}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake"
            DEPENDS "${spv}"
            COMMENT "Embedding ${shader}.spv"
            VERBATIM)
        list(APPEND HCS_SHADER_GENERATED "${gen}")
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Build-time validation of generated GLSL: hcscc lowers the checked-in HCSIR
# fixture, glslc must compile the result. A broken lowering fails the build.
# ---------------------------------------------------------------------------
if(HCS_ENABLE_VULKAN AND HCS_BUILD_TOOLS)
    set(HCS_IR_DIR "${CMAKE_CURRENT_BINARY_DIR}/ir")
    file(MAKE_DIRECTORY "${HCS_IR_DIR}")
    set(HCS_IR_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ir/vecadd.hcsir")
    set(HCS_IR_GLSL "${HCS_IR_DIR}/vecadd.comp")
    set(HCS_IR_SPV "${HCS_IR_DIR}/vecadd.spv")
    add_custom_command(
        OUTPUT "${HCS_IR_GLSL}"
        COMMAND $<TARGET_FILE:hcscc> --input "${HCS_IR_SOURCE}" --output "${HCS_IR_GLSL}"
                --stats
        DEPENDS hcscc "${HCS_IR_SOURCE}"
        COMMENT "hcscc: lower HCSIR fixture to GLSL"
        VERBATIM)
    add_custom_command(
        OUTPUT "${HCS_IR_SPV}"
        COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.1 -O "${HCS_IR_GLSL}"
                -o "${HCS_IR_SPV}"
        DEPENDS "${HCS_IR_GLSL}"
        COMMENT "glslc: validate generated GLSL from hcscc"
        VERBATIM)
    set(HCS_IR_EMBED "${HCS_IR_DIR}/spv_ir_vecadd.cpp")
    add_custom_command(
        OUTPUT "${HCS_IR_EMBED}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${HCS_IR_SPV}" "-DOUTPUT=${HCS_IR_EMBED}"
                "-DNAME=ir_vecadd"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake"
        DEPENDS "${HCS_IR_SPV}"
        COMMENT "Embedding generated HCSIR SPIR-V"
        VERBATIM)
    add_library(hcsuda_ir_spv STATIC "${HCS_IR_EMBED}")
    add_custom_target(hcs_ir_shader_check ALL DEPENDS "${HCS_IR_SPV}")
endif()

# ---------------------------------------------------------------------------
# Core runtime library
# ---------------------------------------------------------------------------
add_library(hcsuda_core STATIC
    src/core/runtime.cpp
    src/ir/hcsir.cpp
    src/ir/lower_glsl.cpp
    src/backends/cpu/cpu_backend.cpp
    src/backends/vulkan/vk_backend.cpp
    ${HCS_SHADER_GENERATED})

target_include_directories(hcsuda_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(hcsuda_core PRIVATE
    HCS_ENABLE_VULKAN=$<BOOL:${HCS_ENABLE_VULKAN}>
    HCS_USE_VALIDATION_LAYERS=$<BOOL:${HCS_VALIDATION_LAYERS}>)

if(HCS_ENABLE_VULKAN)
    target_include_directories(hcsuda_core PRIVATE "${VULKAN_INCLUDE_DIR}")
endif()

if(MSVC)
    target_compile_options(hcsuda_core PRIVATE /W4 /permissive- /utf-8 /EHsc)
    if(HCS_WARNINGS_AS_ERRORS)
        target_compile_options(hcsuda_core PRIVATE /WX)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
if(HCS_BUILD_TOOLS)
    add_executable(hcsdoctor tools/hcsdoctor/main.cpp)
    target_link_libraries(hcsdoctor PRIVATE hcsuda_core)

    add_executable(hcsbench tools/hcsbench/main.cpp)
    target_link_libraries(hcsbench PRIVATE hcsuda_core)

    add_executable(hcscc tools/hcscc/main.cpp)
    target_link_libraries(hcscc PRIVATE hcsuda_core)
    target_include_directories(hcscc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_definitions(hcscc PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    foreach(tool hcsdoctor hcsbench hcscc)
        if(MSVC)
            target_compile_options(${tool} PRIVATE /W4 /permissive- /utf-8)
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(HCS_BUILD_TESTS)
    enable_testing()
    add_executable(hcs_tests
        tests/unit/test_main.cpp
        tests/unit/test_api.cpp
        tests/unit/test_cpu.cpp
        tests/unit/test_flows.cpp
        tests/unit/test_graph.cpp
        tests/unit/test_ir.cpp
        tests/unit/test_ir_glsl.cpp
        tests/unit/test_ir_vulkan.cpp
        tests/unit/test_vulkan.cpp)
    target_link_libraries(hcs_tests PRIVATE hcsuda_core)
    if(HCS_ENABLE_VULKAN)
        target_link_libraries(hcs_tests PRIVATE hcsuda_ir_spv)
    endif()
    target_include_directories(hcs_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options(hcs_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME hcs_unit COMMAND hcs_tests)
endif()

message(STATUS "HCSUDA ${PROJECT_VERSION}: Vulkan backend ${HCS_ENABLE_VULKAN}, tests ${HCS_BUILD_TESTS}")
