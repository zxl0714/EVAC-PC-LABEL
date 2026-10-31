cmake_minimum_required(VERSION 3.25)
set(CMAKE_ASM_COMPILER "${CMAKE_CXX_COMPILER}")
set(CMAKE_C_COMPILER "${CMAKE_CXX_COMPILER}")
project(KetsuiPC LANGUAGES C CXX ASM)

if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "The native Windows x64 probe requires clang-cl for its SysV ABI bridge")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "A 64-bit compiler is required")
endif()

get_filename_component(CAVE_PS4_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CAVE_PS4_DEPS "${CAVE_PS4_ROOT}/.deps/ps4")
set(CAVE_PS4_COMPONENTS "${CAVE_PS4_ROOT}/third_party/ps4")
add_subdirectory("${CAVE_PS4_ROOT}/common" "${CMAKE_BINARY_DIR}/common")

set(ZYAN_ZYCORE_PATH "${CAVE_PS4_DEPS}/Zycore" CACHE PATH "Pinned Zycore" FORCE)
foreach(option ZYDIS_BUILD_SHARED_LIB ZYDIS_BUILD_EXAMPLES ZYDIS_BUILD_TOOLS ZYDIS_BUILD_TESTS ZYDIS_BUILD_DOXYGEN)
    set(${option} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${CAVE_PS4_DEPS}/Zydis" "cpu/zydis" EXCLUDE_FROM_ALL)
add_library(Ps4CpuCompatibility STATIC cpu/compatibility.cpp cpu/function_ranges.cpp)
target_compile_features(Ps4CpuCompatibility PUBLIC cxx_std_20)
target_compile_definitions(Ps4CpuCompatibility PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
# Guarded direct stores must retain SEH around memory accesses so the normal
# write-fault tracker can run before invalid pointers return to the caller.
target_compile_options(Ps4CpuCompatibility PRIVATE /W4 /WX /EHa)
target_link_libraries(Ps4CpuCompatibility PUBLIC Zydis)
target_include_directories(Ps4CpuCompatibility PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

set(KETSUI_VULKAN_HEADERS "${CAVE_PS4_DEPS}/Vulkan-Headers/include")
if(NOT EXISTS "${KETSUI_VULKAN_HEADERS}/vulkan/vulkan.h")
    message(FATAL_ERROR "Run scripts/ps4_sources.py restore to prepare Vulkan-Headers")
endif()
add_library(KetsuiGraphics STATIC graphics/display_layout.cpp graphics/vulkan_device.cpp graphics/pm4.cpp graphics/raster_state.cpp graphics/texture.cpp graphics/shader_compiler.cpp graphics/shader_binary.cpp graphics/shader_tests.cpp graphics/attachment_tests.cpp)
add_library(KetsuiAudio STATIC audio/wasapi_engine.cpp audio/pcm_voice.cpp audio/hevag.cpp audio/atrac9.cpp)
add_library(KetsuiDiagnostics STATIC native_async_log.cpp)
target_compile_features(KetsuiDiagnostics PUBLIC cxx_std_20)
target_compile_definitions(KetsuiDiagnostics PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(KetsuiDiagnostics PRIVATE /W4 /WX /EHsc)
target_link_libraries(KetsuiAudio PRIVATE KetsuiDiagnostics)
if(NOT EXISTS "${CAVE_PS4_DEPS}/LibAtrac9/C/src/libatrac9.c")
    message(FATAL_ERROR "Run scripts/ps4_sources.py restore to prepare pinned LibAtrac9")
endif()
file(GLOB KETSUI_ATRAC9_SOURCES CONFIGURE_DEPENDS "${CAVE_PS4_DEPS}/LibAtrac9/C/src/*.c")
add_library(KetsuiAtrac9 STATIC ${KETSUI_ATRAC9_SOURCES})
target_compile_definitions(KetsuiAtrac9 PRIVATE _USE_MATH_DEFINES)
target_include_directories(KetsuiAtrac9 PUBLIC "${CAVE_PS4_DEPS}/LibAtrac9/C/src")
target_link_libraries(KetsuiAudio PRIVATE KetsuiAtrac9)
add_library(KetsuiContent STATIC content/sfo.cpp content/content_view.cpp content/read_patches.cpp)
target_link_libraries(KetsuiContent PRIVATE bcrypt)
add_library(KetsuiSave STATIC storage/save_store.cpp)
if(NOT EXISTS "${CAVE_PS4_DEPS}/nlohmann-json/single_include/nlohmann/json.hpp")
    message(FATAL_ERROR "Run scripts/ps4_sources.py restore to prepare pinned JSON headers")
endif()
if(NOT EXISTS "${CAVE_PS4_DEPS}/zlib/inflate.c")
    message(FATAL_ERROR "Run scripts/ps4_sources.py restore to prepare pinned zlib")
endif()
# Memory-only codec. Do not build gzip file I/O, tools or a separate DLL.
add_library(KetsuiZlib STATIC "${CAVE_PS4_DEPS}/zlib/adler32.c" "${CAVE_PS4_DEPS}/zlib/crc32.c" "${CAVE_PS4_DEPS}/zlib/inflate.c"
    "${CAVE_PS4_DEPS}/zlib/inftrees.c" "${CAVE_PS4_DEPS}/zlib/inffast.c" "${CAVE_PS4_DEPS}/zlib/zutil.c"
    "${CAVE_PS4_DEPS}/zlib/compress.c" "${CAVE_PS4_DEPS}/zlib/deflate.c" "${CAVE_PS4_DEPS}/zlib/trees.c")

target_include_directories(KetsuiZlib SYSTEM PUBLIC "${CAVE_PS4_DEPS}/zlib")

target_compile_definitions(KetsuiZlib PUBLIC Z_PREFIX)
# Plain SELF decoding is shared by executable and resource preparation.
add_library(KetsuiResourcePreparation STATIC content/resource_prepare.cpp loader/plain_self.cpp)
target_compile_features(KetsuiResourcePreparation PUBLIC cxx_std_20)
target_compile_definitions(KetsuiResourcePreparation PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(KetsuiResourcePreparation PRIVATE /W4 /WX /EHsc)
target_include_directories(KetsuiResourcePreparation SYSTEM PUBLIC "${CAVE_PS4_DEPS}/nlohmann-json/single_include")
target_link_libraries(KetsuiResourcePreparation PRIVATE KetsuiContent KetsuiZlib cave_common bcrypt)
add_library(KetsuiPreparedInputs STATIC loader/elf_prepare.cpp loader/preparation_io.cpp loader/prepared_cache.cpp loader/prepare_game.cpp)
target_compile_features(KetsuiPreparedInputs PUBLIC cxx_std_20)
target_compile_definitions(KetsuiPreparedInputs PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(KetsuiPreparedInputs PRIVATE /W4 /WX /EHsc)
target_link_libraries(KetsuiPreparedInputs PUBLIC cave_common KetsuiResourcePreparation PRIVATE KetsuiContent bcrypt)
# Differential-test driver, not part of the player launch or Public package.
add_executable(KetsuiResourcePrepareCheck content/resource_prepare_check.cpp)
target_compile_features(KetsuiResourcePrepareCheck PRIVATE cxx_std_20)
target_compile_definitions(KetsuiResourcePrepareCheck PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(KetsuiResourcePrepareCheck PRIVATE /W4 /WX /EHsc)
target_link_options(KetsuiResourcePrepareCheck PRIVATE /DEBUG:FULL)
target_link_libraries(KetsuiResourcePrepareCheck PRIVATE KetsuiResourcePreparation KetsuiPreparedInputs)
add_executable(Ps4CompatibilityTests "${CAVE_PS4_ROOT}/tests/ps4_compatibility_test.cpp")
target_include_directories(Ps4CompatibilityTests PRIVATE "${CAVE_PS4_ROOT}")
target_compile_features(Ps4CompatibilityTests PRIVATE cxx_std_20)
target_link_libraries(Ps4CompatibilityTests PRIVATE KetsuiResourcePreparation cave_common)
enable_testing()
add_test(NAME ps4_compatibility COMMAND Ps4CompatibilityTests)
set_tests_properties(ps4_compatibility PROPERTIES TIMEOUT 15)
add_library(KetsuiCommunity STATIC community/protocol.cpp community/client.cpp community/client_tests.cpp community/replay.cpp community/round_journal.cpp community/round_journal_tests.cpp community/round_delivery.cpp)
target_compile_features(KetsuiCommunity PUBLIC cxx_std_20)
target_compile_definitions(KetsuiCommunity PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(KetsuiCommunity PRIVATE /W4 /WX /EHsc)
target_include_directories(KetsuiCommunity SYSTEM PUBLIC "${CAVE_PS4_DEPS}/nlohmann-json/single_include")
target_link_libraries(KetsuiCommunity PUBLIC cave_common KetsuiSave PRIVATE KetsuiZlib bcrypt crypt32 winhttp)
target_compile_features(KetsuiSave PUBLIC cxx_std_20)
target_compile_definitions(KetsuiSave PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(KetsuiSave PRIVATE /W4 /WX /EHsc)
target_link_libraries(KetsuiSave PRIVATE KetsuiContent)
add_subdirectory("${CAVE_PS4_COMPONENTS}/gcn" "${CMAKE_BINARY_DIR}/gcn")

# Input only. Keep our Win32 window, Vulkan presenter and WASAPI audio.
if(NOT EXISTS "${CAVE_PS4_DEPS}/SDL/CMakeLists.txt")
    message(FATAL_ERROR "Run scripts/ps4_sources.py restore to prepare pinned SDL")
endif()
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
foreach(subsystem AUDIO VIDEO RENDER GPU CAMERA SENSOR HAPTIC DIALOG POWER)
    set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${CAVE_PS4_DEPS}/SDL" sdl EXCLUDE_FROM_ALL)
target_link_libraries(KetsuiGraphics PRIVATE KetsuiGcn)
target_link_libraries(KetsuiGraphics PRIVATE SPIRV-Tools-static)
target_compile_features(KetsuiContent PUBLIC cxx_std_20)
target_compile_definitions(KetsuiContent PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(KetsuiContent PRIVATE /W4 /WX /EHsc)
target_compile_features(KetsuiAudio PUBLIC cxx_std_20)
target_compile_definitions(KetsuiAudio PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(KetsuiAudio PRIVATE /W4 /WX /EHsc)
add_library(KetsuiAbi OBJECT sysv_format.s sysv_jump.s)
set_property(TARGET KetsuiAbi PROPERTY MSVC_DEBUG_INFORMATION_FORMAT "")
target_compile_options(KetsuiAbi PRIVATE -Qunused-arguments)
target_compile_features(KetsuiGraphics PUBLIC cxx_std_20)
target_compile_definitions(KetsuiGraphics PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX VK_NO_PROTOTYPES VK_USE_PLATFORM_WIN32_KHR)
target_compile_options(KetsuiGraphics PRIVATE /W4 /WX /EHsc)
target_include_directories(KetsuiGraphics SYSTEM PRIVATE "${KETSUI_VULKAN_HEADERS}")

add_executable(KetsuiNativeProbe
    native_probe.cpp
    native_offline_bench.cpp
    native_async_log_tests.cpp
    collection_config.cpp
    collection_launch.cpp
    loader/prepare_cli.cpp
    guest_heap.cpp
    native_debug.cpp
    native_sync.cpp
    native_semaphore.cpp
    native_event_flag.cpp
    native_threads.cpp
    native_dialogs.cpp
    native_time.cpp
    native_host_timing.cpp
    native_rtc.cpp
    native_libc_time.cpp
    native_user.cpp
    native_screenshot.cpp
    native_storage.cpp
    native_memory.cpp
    memory/write_tracker.cpp
    memory/write_tracker_tests.cpp
    memory/tracked_texture.cpp
    memory/tracked_texture_tests.cpp
    memory/tracked_texture_gpu_tests.cpp
    native_mspace.cpp
    native_video.cpp
    native_libc.cpp
    native_text.cpp
    native_format.cpp
    native_audio.cpp
    native_content.cpp
    native_files.cpp
    native_fios.cpp
    native_gnm.cpp
    native_gpu.cpp
    native_resident_gpu.cpp
    gpu_core/native_memory_backend.cpp
    native_math.cpp
    native_ngs2.cpp
    native_avplayer.cpp
    media/mf_reader.cpp
    media/playback_session.cpp
    $<TARGET_OBJECTS:KetsuiAbi>
    native_profile.cpp
    native_cxx.cpp
    native_equeue.cpp
    native_display_clock.cpp
    native_pad.cpp
    input/keyboard_config.cpp
    input/sdl_gamepad.cpp
    native_np.cpp
    native_np_score.cpp
    native_community_round_audit.cpp
    native_community_rounds.cpp
    native_trophy.cpp
)
target_compile_features(KetsuiNativeProbe PRIVATE cxx_std_20)
target_include_directories(KetsuiNativeProbe PRIVATE "${CAVE_PS4_COMPONENTS}/gpu_core/host" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(KetsuiNativeProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(KetsuiNativeProbe PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc>)
target_link_options(KetsuiNativeProbe PRIVATE /DEBUG:FULL)
target_link_libraries(KetsuiNativeProbe PRIVATE KetsuiGraphics KetsuiAudio KetsuiContent KetsuiSave bcrypt user32 gdi32 comctl32 ole32 windowscodecs mfplat mfreadwrite mfuuid)
target_link_libraries(KetsuiNativeProbe PRIVATE xmllite shlwapi synchronization)
target_link_libraries(KetsuiNativeProbe PRIVATE SDL3::SDL3-static)
target_link_libraries(KetsuiNativeProbe PRIVATE KetsuiCommunity cave_common KetsuiResourcePreparation KetsuiPreparedInputs KetsuiDiagnostics)
target_link_libraries(KetsuiNativeProbe PRIVATE Ps4CpuCompatibility)
add_executable(Ps4CpuCompatibilityTests "${CAVE_PS4_ROOT}/tests/ps4_cpu_test.cpp")
target_link_libraries(Ps4CpuCompatibilityTests PRIVATE Ps4CpuCompatibility)
target_compile_options(Ps4CpuCompatibilityTests PRIVATE /W4 /WX /EHsc)
target_link_options(Ps4CpuCompatibilityTests PRIVATE /DEBUG:FULL)
add_test(NAME ps4_cpu_compatibility COMMAND Ps4CpuCompatibilityTests)
set_tests_properties(ps4_cpu_compatibility PROPERTIES TIMEOUT 30)
execute_process(COMMAND "${CMAKE_CXX_COMPILER}" --print-resource-dir
    OUTPUT_VARIABLE KETSUI_CLANG_RESOURCES OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
set(KETSUI_CLANG_BUILTINS "${KETSUI_CLANG_RESOURCES}/lib/windows/clang_rt.builtins-x86_64.lib")
if(NOT EXISTS "${KETSUI_CLANG_BUILTINS}")
    message(FATAL_ERROR "The clang-cl distribution must include its x64 compiler-rt builtins")
endif()
target_link_libraries(KetsuiNativeProbe PRIVATE "${KETSUI_CLANG_BUILTINS}")
add_test(NAME ps4_profile_dialog COMMAND KetsuiNativeProbe --profile-dialog-self-test)
set_tests_properties(ps4_profile_dialog PROPERTIES TIMEOUT 30)
