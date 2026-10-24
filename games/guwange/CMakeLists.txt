option(CAVE_BUILD_GAME "Build selected games using locally generated code" ON)
cave_game_metadata(guwange)
add_library(guwange_replay STATIC community/replay_format.cpp)
target_include_directories(guwange_replay PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(guwange_replay_tests "${CMAKE_SOURCE_DIR}/tests/guwange/replay_format_test.cpp")
target_link_libraries(guwange_replay_tests PRIVATE guwange_replay)
add_test(NAME guwange_replay COMMAND guwange_replay_tests)
add_library(guwange_community STATIC community/guwange_service.cpp)
target_include_directories(guwange_community PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(guwange_community PUBLIC guwange_replay cave_community_native)
add_executable(guwange_community_tests "${CMAKE_SOURCE_DIR}/tests/guwange/community_service_test.cpp")
target_link_libraries(guwange_community_tests PRIVATE guwange_community)
add_custom_command(TARGET guwange_community_tests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_RUNTIME_DLLS:guwange_community_tests> $<TARGET_FILE_DIR:guwange_community_tests>
    COMMAND_EXPAND_LISTS VERBATIM)
add_test(NAME guwange_community COMMAND guwange_community_tests)
add_test(NAME guwange_community_loopback COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/run_native_score_replay.py" --game guwange --program $<TARGET_FILE:guwange_community_tests>)
set_tests_properties(guwange_community guwange_community_loopback PROPERTIES TIMEOUT 60)

add_executable(guwange_crash_writer ${CMAKE_SOURCE_DIR}/backends/xbox360/diagnostics/crash_writer.cpp)
target_link_libraries(guwange_crash_writer PRIVATE dbghelp psapi)

add_executable(guwange_inspect ${CMAKE_SOURCE_DIR}/backends/xbox360/diagnostics/inspect_xex.cpp)
target_compile_options(guwange_inspect PRIVATE -fno-char8_t -msse4.1)
target_include_directories(guwange_inspect PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/rexglue/thirdparty/inja/third_party/include")
target_link_libraries(guwange_inspect PRIVATE rex::runtime)
add_custom_command(TARGET guwange_inspect POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        $<TARGET_RUNTIME_DLLS:guwange_inspect> $<TARGET_FILE_DIR:guwange_inspect>
    COMMAND_EXPAND_LISTS VERBATIM)

if(CAVE_BUILD_GAME)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/generated/guwange/generation.json")
        message(FATAL_ERROR "Run scripts/Generate.ps1 -Game guwange first")
    endif()
    include("${CMAKE_SOURCE_DIR}/generated/guwange/default/sources.cmake")
    add_executable(guwange WIN32 host.cpp boot_observations.cpp main_wait.cpp community/guest_hooks.cpp ${GENERATED_SOURCES})
    target_include_directories(guwange PRIVATE "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/generated/guwange/default" "${CAVE_GAME_METADATA_DIR}")
    target_compile_options(guwange PRIVATE -fno-char8_t)
    target_link_libraries(guwange PRIVATE cave_xbox360_host guwange_community)
    target_link_options(guwange PRIVATE "LINKER:/MANIFEST:EMBED"
        "LINKER:/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/backends/xbox360/platform/windows.manifest")
    add_dependencies(guwange guwange_crash_writer)
    rexglue_configure_target(guwange GPU_PLUGINS xenos)
    include("${CMAKE_CURRENT_SOURCE_DIR}/overlays.cmake")
endif()

add_executable(cave_guwange_main_wait_tests "${CMAKE_SOURCE_DIR}/tests/xbox360/guwange_main_wait_test.cpp")
target_include_directories(cave_guwange_main_wait_tests PRIVATE "${CMAKE_SOURCE_DIR}")
add_test(NAME guwange_main_wait COMMAND cave_guwange_main_wait_tests)
set_tests_properties(guwange_main_wait PROPERTIES TIMEOUT 5)

add_executable(guwange_input_tests "${CMAKE_SOURCE_DIR}/tests/xbox360/input_test.cpp")
target_link_libraries(guwange_input_tests PRIVATE cave_xbox360_input)
add_test(NAME guwange_input COMMAND guwange_input_tests)
add_custom_command(TARGET guwange_input_tests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        $<TARGET_RUNTIME_DLLS:guwange_input_tests> $<TARGET_FILE_DIR:guwange_input_tests>
    COMMAND_EXPAND_LISTS VERBATIM)
