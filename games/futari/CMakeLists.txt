# Futari shares the maintained SDK, input and HTTP worker with G2.
option(CAVE_BUILD_GAME "Build selected games using locally generated code" ON)
cave_game_metadata(futari)

add_library(futari_community STATIC community/community_client.cpp community/community_runtime.cpp community/replay_format.cpp)
target_include_directories(futari_community PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/community" "${CMAKE_CURRENT_SOURCE_DIR}/diagnostics")
target_link_libraries(futari_community PUBLIC cave_community_http bcrypt crypt32)
add_executable(futari_host_tests ${CMAKE_SOURCE_DIR}/tests/futari/community-client-test.cpp)
target_link_libraries(futari_host_tests PRIVATE futari_community)
add_test(NAME futari_community_native COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/futari/run_native_community.py" --exe $<TARGET_FILE:futari_host_tests>)
set_tests_properties(futari_community_native PROPERTIES TIMEOUT 75)

add_executable(futari_audio_wait_tests ${CMAKE_SOURCE_DIR}/tests/futari/audio-wait-policy-test.cpp
    audio/audio_wait_policy.cpp audio/audio_guest_hooks.cpp)
target_include_directories(futari_audio_wait_tests PRIVATE audio)
target_compile_definitions(futari_audio_wait_tests PRIVATE FUTARI_SCOPED_AUDIO_WAIT_FIX=1)
target_link_libraries(futari_audio_wait_tests PRIVATE rex::runtime winmm)
add_test(NAME futari_audio_wait_on COMMAND futari_audio_wait_tests on)
add_test(NAME futari_audio_wait_off COMMAND futari_audio_wait_tests off)

add_executable(futari_audio_telemetry_tests ${CMAKE_SOURCE_DIR}/tests/futari/audio-telemetry-test.cpp)
target_compile_options(futari_audio_telemetry_tests PRIVATE -UNDEBUG)
target_link_libraries(futari_audio_telemetry_tests PRIVATE rex::runtime)
add_test(NAME futari_audio_telemetry COMMAND futari_audio_telemetry_tests)

add_executable(futari_audio_guest_trace_tests ${CMAKE_SOURCE_DIR}/tests/futari/audio-guest-trace-test.cpp
    audio/audio_guest_trace.cpp audio/audio_guest_hooks.cpp)
target_include_directories(futari_audio_guest_trace_tests PRIVATE audio diagnostics)
target_compile_options(futari_audio_guest_trace_tests PRIVATE -UNDEBUG)
target_compile_definitions(futari_audio_guest_trace_tests PRIVATE FUTARI_AUDIO_DIAGNOSTIC_BUILD=1 FUTARI_AUDIO_GUEST_TEST=1)
target_link_libraries(futari_audio_guest_trace_tests PRIVATE rex::runtime)
add_test(NAME futari_audio_guest COMMAND futari_audio_guest_trace_tests "${CMAKE_BINARY_DIR}/futari-audio-guest-test.log")
set_tests_properties(futari_audio_guest PROPERTIES TIMEOUT 15)

foreach(target futari_host_tests futari_audio_wait_tests futari_audio_telemetry_tests futari_audio_guest_trace_tests)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
        COMMAND_EXPAND_LISTS VERBATIM)
endforeach()

if(CAVE_BUILD_GAME)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/generated/futari/generation.json")
        message(FATAL_ERROR "Run scripts/Generate.ps1 -Game futari first")
    endif()
    include("${CMAKE_SOURCE_DIR}/generated/futari/default/sources.cmake")
    add_executable(futari WIN32 host.cpp compat/module_switch_compat.cpp diagnostics/futari_diagnostics.cpp
        diagnostics/storage_diagnostics.cpp community/community_guest_hooks.cpp
        audio/audio_diagnostics.cpp audio/audio_guest_trace.cpp audio/audio_guest_hooks.cpp audio/audio_wait_policy.cpp
        ${GENERATED_SOURCES})
    target_include_directories(futari PRIVATE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/generated/futari/default"
        "${CAVE_GAME_METADATA_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/audio" "${CMAKE_CURRENT_SOURCE_DIR}/diagnostics")
    target_compile_options(futari PRIVATE -fno-char8_t)
    target_compile_definitions(futari PRIVATE FUTARI_SCOPED_AUDIO_WAIT_FIX=1 FUTARI_AUDIO_DIAGNOSTIC_BUILD=1)
    target_link_libraries(futari PRIVATE cave_xbox360_host futari_community)
    target_link_options(futari PRIVATE "LINKER:/MANIFEST:EMBED"
        "LINKER:/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/backends/xbox360/platform/windows.manifest")
    add_executable(futari_crash_writer ${CMAKE_SOURCE_DIR}/backends/xbox360/diagnostics/crash_writer.cpp)
    target_link_libraries(futari_crash_writer PRIVATE dbghelp psapi)
    add_dependencies(futari futari_crash_writer)
    rexglue_configure_target(futari GPU_PLUGINS xenos)
    include(overlays.cmake)
endif()
