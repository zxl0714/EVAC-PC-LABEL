option(CAVE_BUILD_GAME "Build selected games using locally generated code" ON)
cave_game_metadata(espgaluda2)
add_library(espgaluda2_config STATIC ${CMAKE_SOURCE_DIR}/backends/xbox360/config/settings.cpp)
target_include_directories(espgaluda2_config PUBLIC "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/backends/xbox360" "${CMAKE_SOURCE_DIR}/games/espgaluda2")
target_link_libraries(espgaluda2_config PUBLIC cave_common)
add_executable(espgaluda2_config_tests ${CMAKE_SOURCE_DIR}/tests/xbox360/settings_test.cpp)
target_link_libraries(espgaluda2_config_tests PRIVATE espgaluda2_config)
enable_testing()
add_test(NAME settings COMMAND espgaluda2_config_tests "${CMAKE_SOURCE_DIR}/config/default.ini")
add_executable(espgaluda2_video_timing_tests ${CMAKE_SOURCE_DIR}/tests/xbox360/guest_video_timing_test.cpp)
target_include_directories(espgaluda2_video_timing_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party/rexglue/include)
add_test(NAME guest_video_timing COMMAND espgaluda2_video_timing_tests)
add_executable(espgaluda2_vblank_gate_tests ${CMAKE_SOURCE_DIR}/tests/xbox360/g2_vblank_gate_test.cpp)
target_include_directories(espgaluda2_vblank_gate_tests PRIVATE "${CMAKE_SOURCE_DIR}")
add_test(NAME g2_vblank_gate COMMAND espgaluda2_vblank_gate_tests)
set_tests_properties(g2_vblank_gate PROPERTIES TIMEOUT 10)
add_executable(espgaluda2_fence_wait_tests ${CMAKE_SOURCE_DIR}/tests/xbox360/guest_fence_wait_test.cpp)
target_include_directories(espgaluda2_fence_wait_tests PRIVATE "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/third_party/rexglue/include")
target_link_libraries(espgaluda2_fence_wait_tests PRIVATE synchronization)
add_test(NAME guest_fence_wait COMMAND espgaluda2_fence_wait_tests)
set_tests_properties(guest_fence_wait PROPERTIES TIMEOUT 15)
add_executable(espgaluda2_frame_timing_tests ${CMAKE_SOURCE_DIR}/tests/xbox360/frame_timing_test.cpp)
target_include_directories(espgaluda2_frame_timing_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party/rexglue/include)
add_test(NAME frame_timing COMMAND espgaluda2_frame_timing_tests)
add_executable(espgaluda2_time_zone_tests ${CMAKE_SOURCE_DIR}/tests/xbox360/time_zone_test.cpp)
target_include_directories(espgaluda2_time_zone_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party/rexglue/include)
target_link_libraries(espgaluda2_time_zone_tests PRIVATE rex::runtime)
add_test(NAME time_zone COMMAND espgaluda2_time_zone_tests)
add_custom_command(TARGET espgaluda2_time_zone_tests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        $<TARGET_RUNTIME_DLLS:espgaluda2_time_zone_tests> $<TARGET_FILE_DIR:espgaluda2_time_zone_tests>
    COMMAND_EXPAND_LISTS VERBATIM)
add_library(espgaluda2_community STATIC ${CMAKE_SOURCE_DIR}/games/espgaluda2/community/g2_service.cpp ${CMAKE_SOURCE_DIR}/games/espgaluda2/community/replay_format.cpp)
target_include_directories(espgaluda2_community PUBLIC "${CMAKE_SOURCE_DIR}/backends/xbox360" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(espgaluda2_community PUBLIC cave_community_native)
add_executable(espgaluda2_community_service_tests ${CMAKE_SOURCE_DIR}/tests/xbox360/community_service_test.cpp)
target_link_libraries(espgaluda2_community_service_tests PRIVATE espgaluda2_community)
add_test(NAME community_service COMMAND espgaluda2_community_service_tests)
set_tests_properties(community_service PROPERTIES TIMEOUT 15)
add_test(NAME community_loopback COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/run_native_score_replay.py" --game espgaluda2 --program $<TARGET_FILE:espgaluda2_community_service_tests>)
set_tests_properties(community_loopback PROPERTIES TIMEOUT 60)
add_custom_command(TARGET espgaluda2_community_service_tests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        $<TARGET_RUNTIME_DLLS:espgaluda2_community_service_tests> $<TARGET_FILE_DIR:espgaluda2_community_service_tests>
    COMMAND_EXPAND_LISTS VERBATIM)
add_executable(espgaluda2_community_http_tests ${CMAKE_SOURCE_DIR}/tests/xbox360/community_http_test.cpp)
target_link_libraries(espgaluda2_community_http_tests PRIVATE cave_community_http)
add_test(NAME community_http COMMAND espgaluda2_community_http_tests)
set_tests_properties(community_http PROPERTIES TIMEOUT 10)
add_custom_command(TARGET espgaluda2_community_http_tests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        $<TARGET_RUNTIME_DLLS:espgaluda2_community_http_tests> $<TARGET_FILE_DIR:espgaluda2_community_http_tests>
    COMMAND_EXPAND_LISTS VERBATIM)
add_executable(espgaluda2_input_tests ${CMAKE_SOURCE_DIR}/tests/xbox360/input_test.cpp)
target_link_libraries(espgaluda2_input_tests PRIVATE cave_xbox360_input)
add_test(NAME input COMMAND espgaluda2_input_tests)
add_custom_command(TARGET espgaluda2_input_tests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        $<TARGET_RUNTIME_DLLS:espgaluda2_input_tests> $<TARGET_FILE_DIR:espgaluda2_input_tests>
    COMMAND_EXPAND_LISTS VERBATIM)
add_library(espgaluda2_diagnostics ALIAS cave_xbox360_diagnostics)
add_executable(espgaluda2_preflight ${CMAKE_SOURCE_DIR}/games/espgaluda2/tools/preflight.cpp)
target_link_libraries(espgaluda2_preflight PRIVATE espgaluda2_diagnostics)
target_include_directories(espgaluda2_preflight PRIVATE "${CAVE_GAME_METADATA_DIR}")
target_link_options(espgaluda2_preflight PRIVATE "LINKER:/MANIFEST:EMBED"
    "LINKER:/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/backends/xbox360/platform/windows.manifest")
add_executable(espgaluda2_hash_tests ${CMAKE_SOURCE_DIR}/tests/xbox360/hash_test.cpp)
target_link_libraries(espgaluda2_hash_tests PRIVATE espgaluda2_diagnostics)
add_test(NAME hashing COMMAND espgaluda2_hash_tests)
add_executable(espgaluda2_logging_tests ${CMAKE_SOURCE_DIR}/tests/xbox360/async_log_test.cpp)
target_include_directories(espgaluda2_logging_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party/rexglue/src/core)
target_link_libraries(espgaluda2_logging_tests PRIVATE rex::runtime)
add_test(NAME async_logging COMMAND espgaluda2_logging_tests)
set_tests_properties(async_logging PROPERTIES TIMEOUT 10)
add_library(espgaluda2_crash ALIAS cave_xbox360_crash)
add_executable(espgaluda2_crash_writer ${CMAKE_SOURCE_DIR}/backends/xbox360/diagnostics/crash_writer.cpp)
target_link_libraries(espgaluda2_crash_writer PRIVATE dbghelp psapi)
add_executable(espgaluda2_crash_tests ${CMAKE_SOURCE_DIR}/tests/xbox360/crash_test.cpp)
target_link_libraries(espgaluda2_crash_tests PRIVATE espgaluda2_crash)
add_dependencies(espgaluda2_crash_tests espgaluda2_crash_writer)

# Deliberately a console-only analysis target, not a game/player entry point.
add_executable(espgaluda2_inspect ${CMAKE_SOURCE_DIR}/backends/xbox360/diagnostics/inspect_xex.cpp)
target_compile_options(espgaluda2_inspect PRIVATE -fno-char8_t -msse4.1)
target_include_directories(espgaluda2_inspect PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/rexglue/thirdparty/inja/third_party/include")
target_link_libraries(espgaluda2_inspect PRIVATE rex::runtime)
add_custom_command(TARGET espgaluda2_inspect POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        $<TARGET_RUNTIME_DLLS:espgaluda2_inspect> $<TARGET_FILE_DIR:espgaluda2_inspect>
    COMMAND_EXPAND_LISTS VERBATIM)

# The portable Windows LLVM archive does not include the PowerPC target.
# Use the same Xenon-aware decoder as the maintained SDK's analyzer.
add_library(espgaluda2_disasm STATIC
    ${CMAKE_SOURCE_DIR}/third_party/rexglue/thirdparty/disasm/disasm.c
    ${CMAKE_SOURCE_DIR}/third_party/rexglue/thirdparty/disasm/ppc-dis.c)
target_include_directories(espgaluda2_disasm PUBLIC ${CMAKE_SOURCE_DIR}/third_party/rexglue/thirdparty/disasm)
add_executable(espgaluda2_ppc_disasm ${CMAKE_SOURCE_DIR}/games/espgaluda2/tools/ppc_disasm.cpp)
target_link_libraries(espgaluda2_ppc_disasm PRIVATE espgaluda2_disasm)

add_executable(espgaluda2_sdk_tests ${CMAKE_SOURCE_DIR}/games/espgaluda2/tools/sdk_memory_test.cpp)
target_link_libraries(espgaluda2_sdk_tests PRIVATE rex::runtime)
add_custom_command(TARGET espgaluda2_sdk_tests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        $<TARGET_RUNTIME_DLLS:espgaluda2_sdk_tests> $<TARGET_FILE_DIR:espgaluda2_sdk_tests>
    COMMAND_EXPAND_LISTS VERBATIM)

if(CAVE_BUILD_GAME)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/generated/espgaluda2/generation.json")
        message(FATAL_ERROR "Run scripts/Generate.ps1 and resolve all validation errors first")
    endif()
    include("${CMAKE_SOURCE_DIR}/generated/espgaluda2/default/sources.cmake")
    add_executable(espgaluda2 WIN32 ${CMAKE_SOURCE_DIR}/games/espgaluda2/host.cpp ${CMAKE_SOURCE_DIR}/games/espgaluda2/community/guest_hooks.cpp ${GENERATED_SOURCES})
    target_sources(espgaluda2 PRIVATE latency/host.cpp performance/fence_wait.cpp)
    target_include_directories(espgaluda2 PRIVATE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/generated/espgaluda2/default" "${CAVE_GAME_METADATA_DIR}")
    target_compile_options(espgaluda2 PRIVATE -fno-char8_t)
    target_link_libraries(espgaluda2 PRIVATE cave_xbox360_host espgaluda2_community synchronization)
    target_link_options(espgaluda2 PRIVATE "LINKER:/MANIFEST:EMBED"
        "LINKER:/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/backends/xbox360/platform/windows.manifest")
    add_dependencies(espgaluda2 espgaluda2_crash_writer)
    rexglue_configure_target(espgaluda2 GPU_PLUGINS xenos)
    include("${CMAKE_CURRENT_SOURCE_DIR}/overlays.cmake")
    add_executable(espgaluda2_module_tests ${CMAKE_SOURCE_DIR}/games/espgaluda2/tools/module_lifecycle_test.cpp)
    target_link_libraries(espgaluda2_module_tests PRIVATE rex::runtime)
    add_dependencies(espgaluda2_module_tests espgaluda2)
endif()
