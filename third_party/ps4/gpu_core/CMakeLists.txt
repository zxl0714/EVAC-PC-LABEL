cmake_minimum_required(VERSION 3.25)
project(KetsuiGpuCore LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "This integration target currently requires Windows x64 and clang-cl")
endif()

get_filename_component(KETSUI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(DEPS "${KETSUI_ROOT}/.deps/ps4")
set(BACKEND "${KETSUI_ROOT}/backends/ps4")
foreach(REQUIRED Vulkan-Headers/include/vulkan/vulkan.hpp fmt/CMakeLists.txt
                 ext-boost/boost/container/static_vector.hpp magic_enum/include/magic_enum/magic_enum.hpp
                 robin-map/include/tsl/robin_map.h tracy/public/tracy/Tracy.hpp vma/include/vk_mem_alloc.h
                 half/include/half.hpp glslang/CMakeLists.txt abseil-cpp/CMakeLists.txt xxhash/xxhash.c)
    if(NOT EXISTS "${DEPS}/${REQUIRED}")
        message(FATAL_ERROR "Missing ${REQUIRED}; run scripts/ps4_sources.py restore")
    endif()
endforeach()

set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${DEPS}/fmt" "${CMAKE_BINARY_DIR}/deps/fmt" EXCLUDE_FROM_ALL)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
add_subdirectory("${DEPS}/glslang" "${CMAKE_BINARY_DIR}/deps/glslang" EXCLUDE_FROM_ALL)
include(HostShaders.cmake)
set(CMAKE_CXX_STANDARD 23)
set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
add_subdirectory("${DEPS}/abseil-cpp" "${CMAKE_BINARY_DIR}/deps/abseil-cpp" EXCLUDE_FROM_ALL)
add_library(KetsuiGpuXxHash STATIC "${DEPS}/xxhash/xxhash.c")
target_include_directories(KetsuiGpuXxHash SYSTEM PUBLIC "${DEPS}/xxhash")

set(GPU_SOURCES
    upstream/src/video_core/renderer_vulkan/vk_common.cpp
    upstream/src/video_core/renderer_vulkan/vk_instance.cpp
    upstream/src/video_core/renderer_vulkan/vk_platform.cpp
    upstream/src/video_core/renderer_vulkan/vk_resource_pool.cpp
    upstream/src/video_core/renderer_vulkan/vk_scheduler.cpp
    upstream/src/video_core/renderer_vulkan/vk_semaphore.cpp
    upstream/src/video_core/renderer_vulkan/vk_staging_buffer_pool.cpp
    upstream/src/video_core/renderer_vulkan/vk_runtime.cpp
    upstream/src/video_core/renderer_vulkan/vk_shader_util.cpp
    upstream/src/video_core/renderer_vulkan/liverpool_to_vk.cpp
    upstream/src/video_core/amdgpu/pixel_format.cpp
    upstream/src/video_core/amdgpu/regs.cpp
    upstream/src/video_core/buffer_cache/buffer.cpp
    upstream/src/video_core/buffer_cache/buffer_cache.cpp
    upstream/src/video_core/buffer_cache/fault_manager.cpp
    upstream/src/video_core/texture_cache/texture_cache.cpp
    upstream/src/video_core/texture_cache/sampler.cpp
    upstream/src/video_core/texture_cache/tile_manager.cpp
    upstream/src/video_core/page_manager.cpp
    upstream/src/video_core/texture_cache/image.cpp
    upstream/src/video_core/texture_cache/image_info.cpp
    upstream/src/video_core/texture_cache/image_view.cpp
    upstream/src/video_core/texture_cache/host_compatibility.cpp
    upstream/src/video_core/texture_cache/blit_helper.cpp
    upstream/src/common/number_utils.cpp
    upstream/src/common/slot_vector.cpp
    upstream/src/common/spin_lock.cpp
    upstream/src/common/recursive_lock.cpp
    upstream/src/common/error.cpp
    upstream/src/video_core/amdgpu/tiling.cpp
    host/host_services.cpp host/cache_memory.cpp)
add_library(KetsuiGpuCore STATIC ${GPU_SOURCES})
add_dependencies(KetsuiGpuCore KetsuiGpuHostShaders)
target_compile_features(KetsuiGpuCore PUBLIC cxx_std_23)
target_compile_options(KetsuiGpuCore PRIVATE /EHsc /W3)
target_compile_definitions(KetsuiGpuCore PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(KetsuiGpuCore PUBLIC host upstream/src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(KetsuiGpuCore SYSTEM PUBLIC
    "${DEPS}/Vulkan-Headers/include" "${DEPS}/ext-boost"
    "${DEPS}/magic_enum/include" "${DEPS}/robin-map/include"
    "${DEPS}/tracy/public" "${DEPS}/vma/include" "${DEPS}/half/include")
target_link_libraries(KetsuiGpuCore PUBLIC fmt::fmt KetsuiGpuXxHash absl::flat_hash_map absl::flat_hash_set)

file(GLOB_RECURSE GPU_SIRIT_SOURCES CONFIGURE_DEPENDS "${DEPS}/sirit/src/*.cpp")
add_library(KetsuiGpuSirit STATIC ${GPU_SIRIT_SOURCES})
target_compile_features(KetsuiGpuSirit PUBLIC cxx_std_23)
target_compile_options(KetsuiGpuSirit PRIVATE /EHsc /W3)
target_include_directories(KetsuiGpuSirit PUBLIC "${DEPS}/sirit/include" "${DEPS}/SPIRV-Headers/include")
target_include_directories(KetsuiGpuSirit PRIVATE "${DEPS}/sirit/src")

file(GLOB_RECURSE GPU_RECOMPILER_SOURCES CONFIGURE_DEPENDS "upstream/src/shader_recompiler/*.cpp")
add_library(KetsuiGpuRecompiler STATIC ${GPU_RECOMPILER_SOURCES}
    host/srt_interpreter.cpp upstream/src/common/io_file.cpp)
target_compile_features(KetsuiGpuRecompiler PUBLIC cxx_std_23)
target_compile_options(KetsuiGpuRecompiler PRIVATE /EHsc /W3)
target_include_directories(KetsuiGpuRecompiler PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/upstream")
foreach(BOOST_PART align range optional utility tuple iterator)
    target_include_directories(KetsuiGpuRecompiler SYSTEM PUBLIC "${DEPS}/ext-boost/libs/${BOOST_PART}/include")
endforeach()
target_link_libraries(KetsuiGpuRecompiler PUBLIC KetsuiGpuCore KetsuiGpuSirit)

add_library(KetsuiGpuRenderer STATIC
    host/gpu_program.cpp
    upstream/src/video_core/renderer_vulkan/vk_pipeline_common.cpp
    upstream/src/video_core/renderer_vulkan/vk_graphics_pipeline.cpp
    upstream/src/video_core/renderer_vulkan/vk_compute_pipeline.cpp
    upstream/src/video_core/renderer_vulkan/vk_pipeline_cache.cpp
    upstream/src/video_core/renderer_vulkan/vk_rasterizer.cpp)
target_compile_features(KetsuiGpuRenderer PUBLIC cxx_std_23)
target_compile_options(KetsuiGpuRenderer PRIVATE /EHsc /W3)
target_link_libraries(KetsuiGpuRenderer PUBLIC KetsuiGpuRecompiler)

add_library(KetsuiGpuRuntime SHARED host/runtime_api.cpp host/presenter.cpp)
target_compile_features(KetsuiGpuRuntime PRIVATE cxx_std_23)
target_include_directories(KetsuiGpuRuntime PRIVATE "${BACKEND}")
target_compile_options(KetsuiGpuRuntime PRIVATE /EHsc /W4)
target_link_libraries(KetsuiGpuRuntime PRIVATE KetsuiGpuRenderer)
target_link_options(KetsuiGpuRuntime PRIVATE /DEBUG:FULL)

# Reusable bridge, not another allocator. The final host supplies its existing
# native_memory.cpp and diagnostics; only the standalone test links test sinks.
add_library(KetsuiGpuNativeMemory STATIC "${BACKEND}/gpu_core/native_memory_backend.cpp")
target_compile_features(KetsuiGpuNativeMemory PUBLIC cxx_std_23)
target_compile_definitions(KetsuiGpuNativeMemory PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(KetsuiGpuNativeMemory PUBLIC host)
target_compile_options(KetsuiGpuNativeMemory PRIVATE /EHsc /W4)
target_include_directories(KetsuiGpuNativeMemory PUBLIC "${BACKEND}/gpu_core")

add_executable(KetsuiGpuCoreSmoke "${BACKEND}/gpu_core/scheduler_smoke.cpp"
    "${BACKEND}/gpu_core/buffer_pool_tests.cpp" "${BACKEND}/gpu_core/image_runtime_tests.cpp"
    "${BACKEND}/gpu_core/cache_tests.cpp"
    "${BACKEND}/gpu_core/native_memory_tests.cpp" "${BACKEND}/gpu_core/test_diagnostics.cpp"
    "${BACKEND}/gpu_core/recompiler_tests.cpp"
    "${BACKEND}/gpu_core/program_reader_tests.cpp"
    "${BACKEND}/native_memory.cpp" "${BACKEND}/memory/write_tracker.cpp"
    "${BACKEND}/memory/write_tracker_tests.cpp"
    "${BACKEND}/graphics/texture.cpp" "${BACKEND}/graphics/display_layout.cpp")
target_link_libraries(KetsuiGpuCoreSmoke PRIVATE KetsuiGpuNativeMemory KetsuiGpuRenderer)

target_include_directories(KetsuiGpuCoreSmoke PRIVATE "${BACKEND}")
target_compile_options(KetsuiGpuCoreSmoke PRIVATE /EHsc /W4)
target_link_options(KetsuiGpuCoreSmoke PRIVATE /DEBUG:FULL)

enable_testing()
add_executable(KetsuiGpuCompileMetricsSmoke "${BACKEND}/gpu_core/compile_metrics_smoke.cpp")
target_include_directories(KetsuiGpuCompileMetricsSmoke PRIVATE host)
target_compile_features(KetsuiGpuCompileMetricsSmoke PRIVATE cxx_std_20)
target_compile_options(KetsuiGpuCompileMetricsSmoke PRIVATE /EHsc /W4)
target_link_options(KetsuiGpuCompileMetricsSmoke PRIVATE /DEBUG:FULL)
add_test(NAME gpu_compile_metrics COMMAND KetsuiGpuCompileMetricsSmoke)
set_tests_properties(gpu_compile_metrics PROPERTIES TIMEOUT 10)
add_test(NAME gpu_core_scheduler COMMAND KetsuiGpuCoreSmoke)
set_tests_properties(gpu_core_scheduler PROPERTIES TIMEOUT 30)

add_executable(KetsuiGpuRuntimeSmoke "${BACKEND}/gpu_core/runtime_smoke.cpp"
    "${BACKEND}/graphics/display_layout.cpp"
    "${BACKEND}/gpu_core/legacy_symbol_canary.cpp"
    "${BACKEND}/gpu_core/test_diagnostics.cpp"
    "${BACKEND}/native_memory.cpp" "${BACKEND}/memory/write_tracker.cpp"
    "${BACKEND}/memory/write_tracker_tests.cpp")
target_link_libraries(KetsuiGpuRuntimeSmoke PRIVATE KetsuiGpuNativeMemory)
target_include_directories(KetsuiGpuRuntimeSmoke PRIVATE "${BACKEND}")
target_compile_options(KetsuiGpuRuntimeSmoke PRIVATE /EHsc /W4)
target_link_options(KetsuiGpuRuntimeSmoke PRIVATE /DEBUG:FULL)
add_dependencies(KetsuiGpuRuntimeSmoke KetsuiGpuRuntime)
add_test(NAME gpu_runtime_draw COMMAND KetsuiGpuRuntimeSmoke)
set_tests_properties(gpu_runtime_draw PROPERTIES TIMEOUT 30)
add_test(NAME gpu_runtime_present COMMAND KetsuiGpuRuntimeSmoke --presentation)
set_tests_properties(gpu_runtime_present PROPERTIES TIMEOUT 30)
add_test(NAME gpu_runtime_present_no_vsync COMMAND KetsuiGpuRuntimeSmoke --presentation --no-vsync)
set_tests_properties(gpu_runtime_present_no_vsync PROPERTIES TIMEOUT 30)
