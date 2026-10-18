# Futari shares the maintained SDK, input and HTTP worker with G2.
option(CAVE_BUILD_GAME "Build selected games using locally generated code" ON)
if(CAVE_BUILD_GAME)
    message(FATAL_ERROR "Futari host integration is in progress; use -NativeOnly for platform regression")
endif()

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
