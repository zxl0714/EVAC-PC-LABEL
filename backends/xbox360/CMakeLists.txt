# Platform components contain no game addresses, translated bodies or mode list.
add_library(cave_xbox360_input STATIC input/configured_input.cpp input/input_state.cpp)
target_include_directories(cave_xbox360_input PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cave_xbox360_input PUBLIC cave_common rex::runtime)

add_library(cave_community_http STATIC ${CMAKE_SOURCE_DIR}/common/community/http_client.cpp)
target_include_directories(cave_community_http PUBLIC "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/common"
    "${CMAKE_SOURCE_DIR}/third_party/rexglue/thirdparty/inja/third_party/include")
target_link_libraries(cave_community_http PUBLIC rex::runtime winhttp normaliz)

add_library(cave_replay_encoding STATIC ${CMAKE_SOURCE_DIR}/common/community/replay_encoding.cpp)
target_include_directories(cave_replay_encoding PUBLIC "${CMAKE_SOURCE_DIR}/common")
target_link_libraries(cave_replay_encoding PUBLIC bcrypt crypt32)
add_library(cave_community_native STATIC community/native_service.cpp)
target_include_directories(cave_community_native PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cave_community_native PUBLIC cave_community_http cave_replay_encoding)

add_library(cave_xbox360_diagnostics STATIC diagnostics/run_context.cpp)
target_include_directories(cave_xbox360_diagnostics PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cave_xbox360_diagnostics PUBLIC cave_common cave_community_http bcrypt)
add_library(cave_xbox360_crash STATIC diagnostics/crash_reporter.cpp)
target_include_directories(cave_xbox360_crash PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}")
add_library(cave_xbox360_host STATIC host.cpp)
target_include_directories(cave_xbox360_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cave_xbox360_host PUBLIC cave_xbox360_diagnostics cave_xbox360_crash cave_xbox360_input rex::runtime winmm)

add_executable(cave_content_source_tests "${CMAKE_SOURCE_DIR}/tests/xbox360/content_source_test.cpp")
target_link_libraries(cave_content_source_tests PRIVATE rex::runtime)
add_custom_command(TARGET cave_content_source_tests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_RUNTIME_DLLS:cave_content_source_tests> $<TARGET_FILE_DIR:cave_content_source_tests>
    COMMAND_EXPAND_LISTS VERBATIM)
add_test(NAME content_source COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/run_content_source_test.py" $<TARGET_FILE:cave_content_source_tests>)
set_tests_properties(content_source PROPERTIES TIMEOUT 30)
add_test(NAME codegen_paths COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/xbox360/check_codegen_paths.py"
    --generator "${CMAKE_SOURCE_DIR}/.deps/sdk-install/bin/rexglue.exe")
set_tests_properties(codegen_paths PROPERTIES TIMEOUT 30)

add_executable(cave_community_sync_tests "${CMAKE_SOURCE_DIR}/tests/xbox360/community_sync_test.cpp")
target_link_libraries(cave_community_sync_tests PRIVATE rex::runtime)
add_custom_command(TARGET cave_community_sync_tests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_RUNTIME_DLLS:cave_community_sync_tests> $<TARGET_FILE_DIR:cave_community_sync_tests>
    COMMAND_EXPAND_LISTS VERBATIM)
add_test(NAME community_sync COMMAND cave_community_sync_tests "${CMAKE_BINARY_DIR}/community-sync-fixture")
set_tests_properties(community_sync PROPERTIES TIMEOUT 30)

# Metadata is title-specific; the platform libraries do not embed a game ID.
function(cave_game_metadata game)
    set(CAVE_GAME_ID "${game}")
    file(READ "${CMAKE_SOURCE_DIR}/games/${game}/inputs.json" CAVE_XEX_METADATA)
    execute_process(COMMAND git --no-optional-locks rev-parse HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE CAVE_SOURCE_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND git --no-optional-locks status --porcelain --untracked-files=normal --ignore-submodules=dirty
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE dirty OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    set(CAVE_SOURCE_DIRTY "false")
    if(dirty)
        set(CAVE_SOURCE_DIRTY "true")
    endif()
    set(folder "${CMAKE_BINARY_DIR}/include/${game}")
    configure_file("${CMAKE_SOURCE_DIR}/backends/xbox360/diagnostics/build_metadata.h.in" "${folder}/build_metadata.h" @ONLY)
    set(CAVE_GAME_METADATA_DIR "${folder}" PARENT_SCOPE)
endfunction()
