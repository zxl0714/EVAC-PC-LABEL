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

set(KETSUI_VULKAN_HEADERS "${CMAKE_SOURCE_DIR}/.deps/Vulkan-Headers/include")
if(NOT EXISTS "${KETSUI_VULKAN_HEADERS}/vulkan/vulkan.h")
    message(FATAL_ERROR "Run scripts/Prepare-NativeDeps.ps1 to prepare Vulkan-Headers")
endif()
add_library(KetsuiGraphics STATIC src/graphics/display_layout.cpp src/graphics/vulkan_device.cpp src/graphics/pm4.cpp src/graphics/raster_state.cpp src/graphics/texture.cpp src/graphics/shader_compiler.cpp src/graphics/shader_binary.cpp src/graphics/shader_tests.cpp src/graphics/attachment_tests.cpp)
add_library(KetsuiAudio STATIC src/audio/wasapi_engine.cpp src/audio/pcm_voice.cpp src/audio/hevag.cpp src/audio/atrac9.cpp)
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/.deps/LibAtrac9/C/src/libatrac9.c")
    message(FATAL_ERROR "Run scripts/Prepare-NativeDeps.ps1 to prepare pinned LibAtrac9")
endif()
file(GLOB KETSUI_ATRAC9_SOURCES CONFIGURE_DEPENDS ".deps/LibAtrac9/C/src/*.c")
add_library(KetsuiAtrac9 STATIC ${KETSUI_ATRAC9_SOURCES})
target_compile_definitions(KetsuiAtrac9 PRIVATE _USE_MATH_DEFINES)
target_include_directories(KetsuiAtrac9 PUBLIC .deps/LibAtrac9/C/src)
target_link_libraries(KetsuiAudio PRIVATE KetsuiAtrac9)
add_library(KetsuiContent STATIC src/content/sfo.cpp src/content/content_view.cpp src/content/read_patches.cpp)
target_link_libraries(KetsuiContent PRIVATE bcrypt)
add_library(KetsuiSave STATIC src/storage/save_store.cpp)
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/.deps/nlohmann-json/single_include/nlohmann/json.hpp")
    message(FATAL_ERROR "Run scripts/Prepare-NativeDeps.ps1 to prepare pinned JSON headers")
endif()
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/.deps/zlib/inflate.c")
    message(FATAL_ERROR "Run scripts/Prepare-NativeDeps.ps1 to prepare pinned zlib")
endif()
# Memory-only codec. Do not build gzip file I/O, tools or a separate DLL.
add_library(KetsuiZlib STATIC .deps/zlib/adler32.c .deps/zlib/crc32.c .deps/zlib/inflate.c
    .deps/zlib/inftrees.c .deps/zlib/inffast.c .deps/zlib/zutil.c
    .deps/zlib/compress.c .deps/zlib/deflate.c .deps/zlib/trees.c)

target_include_directories(KetsuiZlib SYSTEM PUBLIC .deps/zlib)

target_compile_definitions(KetsuiZlib PUBLIC Z_PREFIX)
add_library(KetsuiCommunity STATIC src/community/protocol.cpp src/community/client.cpp src/community/client_tests.cpp src/community/replay.cpp src/community/round_journal.cpp src/community/round_journal_tests.cpp src/community/round_delivery.cpp)
target_compile_features(KetsuiCommunity PUBLIC cxx_std_20)
target_compile_definitions(KetsuiCommunity PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(KetsuiCommunity PRIVATE /W4 /WX /EHsc)
target_include_directories(KetsuiCommunity SYSTEM PUBLIC .deps/nlohmann-json/single_include)
target_link_libraries(KetsuiCommunity PUBLIC KetsuiSave PRIVATE KetsuiZlib bcrypt crypt32 winhttp)
target_compile_features(KetsuiSave PUBLIC cxx_std_20)
target_compile_definitions(KetsuiSave PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(KetsuiSave PRIVATE /W4 /WX /EHsc)
target_link_libraries(KetsuiSave PRIVATE KetsuiContent)
add_subdirectory(third_party/gcn)

# Input only. Keep our Win32 window, Vulkan presenter and WASAPI audio.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/.deps/SDL/CMakeLists.txt")
    message(FATAL_ERROR "Run scripts/Prepare-NativeDeps.ps1 to prepare pinned SDL")
endif()
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
foreach(subsystem AUDIO VIDEO RENDER GPU CAMERA SENSOR HAPTIC DIALOG POWER)
    set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory(.deps/SDL sdl EXCLUDE_FROM_ALL)
target_link_libraries(KetsuiGraphics PRIVATE KetsuiGcn)
target_link_libraries(KetsuiGraphics PRIVATE SPIRV-Tools-static)
target_compile_features(KetsuiContent PUBLIC cxx_std_20)
target_compile_definitions(KetsuiContent PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(KetsuiContent PRIVATE /W4 /WX /EHsc)
target_compile_features(KetsuiAudio PUBLIC cxx_std_20)
target_compile_definitions(KetsuiAudio PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(KetsuiAudio PRIVATE /W4 /WX /EHsc)
add_library(KetsuiAbi OBJECT src/sysv_format.s src/sysv_jump.s)
set_property(TARGET KetsuiAbi PROPERTY MSVC_DEBUG_INFORMATION_FORMAT "")
target_compile_options(KetsuiAbi PRIVATE -Qunused-arguments)
target_compile_features(KetsuiGraphics PUBLIC cxx_std_20)
target_compile_definitions(KetsuiGraphics PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX VK_NO_PROTOTYPES VK_USE_PLATFORM_WIN32_KHR)
target_compile_options(KetsuiGraphics PRIVATE /W4 /WX /EHsc)
target_include_directories(KetsuiGraphics SYSTEM PRIVATE "${KETSUI_VULKAN_HEADERS}")

add_executable(KetsuiNativeProbe
    src/native_probe.cpp
    src/guest_heap.cpp
    src/native_debug.cpp
    src/native_sync.cpp
    src/native_semaphore.cpp
    src/native_event_flag.cpp
    src/native_threads.cpp
    src/native_dialogs.cpp
    src/native_time.cpp
    src/native_host_timing.cpp
    src/native_rtc.cpp
    src/native_libc_time.cpp
    src/native_user.cpp
    src/native_screenshot.cpp
    src/native_storage.cpp
    src/native_memory.cpp
    src/memory/write_tracker.cpp
    src/memory/write_tracker_tests.cpp
    src/memory/tracked_texture.cpp
    src/memory/tracked_texture_tests.cpp
    src/memory/tracked_texture_gpu_tests.cpp
    src/native_mspace.cpp
    src/native_video.cpp
    src/native_libc.cpp
    src/native_text.cpp
    src/native_format.cpp
    src/native_audio.cpp
    src/native_content.cpp
    src/native_files.cpp
    src/native_fios.cpp
    src/native_gnm.cpp
    src/native_gpu.cpp
    src/native_resident_gpu.cpp
    src/gpu_core/native_memory_backend.cpp
    src/native_math.cpp
    src/native_ngs2.cpp
    src/native_avplayer.cpp
    src/media/mf_reader.cpp
    src/media/playback_session.cpp
    $<TARGET_OBJECTS:KetsuiAbi>
    src/native_profile.cpp
    src/native_cxx.cpp
    src/native_equeue.cpp
    src/native_display_clock.cpp
    src/native_pad.cpp
    src/input/keyboard_config.cpp
    src/input/sdl_gamepad.cpp
    src/native_np.cpp
    src/native_np_score.cpp
    src/native_community_round_audit.cpp
    src/native_community_rounds.cpp
    src/native_trophy.cpp
)
target_compile_features(KetsuiNativeProbe PRIVATE cxx_std_20)
target_include_directories(KetsuiNativeProbe PRIVATE third_party/gpu_core/host)
target_compile_definitions(KetsuiNativeProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(KetsuiNativeProbe PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc>)
target_link_options(KetsuiNativeProbe PRIVATE /DEBUG:FULL)
target_link_libraries(KetsuiNativeProbe PRIVATE KetsuiGraphics KetsuiAudio KetsuiContent KetsuiSave bcrypt user32 gdi32 comctl32 ole32 windowscodecs mfplat mfreadwrite mfuuid)
target_link_libraries(KetsuiNativeProbe PRIVATE xmllite shlwapi)
target_link_libraries(KetsuiNativeProbe PRIVATE SDL3::SDL3-static)
target_link_libraries(KetsuiNativeProbe PRIVATE KetsuiCommunity)
execute_process(COMMAND "${CMAKE_CXX_COMPILER}" --print-resource-dir
    OUTPUT_VARIABLE KETSUI_CLANG_RESOURCES OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
set(KETSUI_CLANG_BUILTINS "${KETSUI_CLANG_RESOURCES}/lib/windows/clang_rt.builtins-x86_64.lib")
if(NOT EXISTS "${KETSUI_CLANG_BUILTINS}")
    message(FATAL_ERROR "The clang-cl distribution must include its x64 compiler-rt builtins")
endif()
target_link_libraries(KetsuiNativeProbe PRIVATE "${KETSUI_CLANG_BUILTINS}")
