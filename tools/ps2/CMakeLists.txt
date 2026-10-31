cmake_minimum_required(VERSION 3.25)
project(EVACPS2Tools LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
get_filename_component(COLLECTION_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(DEPS "${COLLECTION_ROOT}/.deps/ps2")
set(UPSTREAM "${DEPS}/PS2Recomp")

# Sources are restored and verified before configure. No FetchContent downloads,
# upstream release flags, game-specific generated code or system library fallback.
set(ELFIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ELFIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TOML11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(BUILD_DWARFDUMP OFF CACHE BOOL "" FORCE)
set(BUILD_DWARFEXAMPLE OFF CACHE BOOL "" FORCE)
set(BUILD_DWARFGEN OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_NON_SHARED ON CACHE BOOL "" FORCE)
set(ENABLE_DECOMPRESSION OFF CACHE BOOL "" FORCE)
foreach(dependency elfio toml11 fmt libdwarf nlohmann_json)
  add_subdirectory("${DEPS}/${dependency}" "deps/${dependency}" EXCLUDE_FROM_ALL)
endforeach()

file(GLOB_RECURSE RABBITIZER_SOURCES CONFIGURE_DEPENDS
  "${DEPS}/rabbitizer/src/analysis/*.c"
  "${DEPS}/rabbitizer/src/common/*.c"
  "${DEPS}/rabbitizer/src/instructions/*.c")
add_library(ps2_rabbitizer STATIC ${RABBITIZER_SOURCES})
target_include_directories(ps2_rabbitizer PUBLIC
  "${DEPS}/rabbitizer/include" "${DEPS}/rabbitizer/tables")

file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS "${UPSTREAM}/ps2xRecomp/src/lib/*.cpp")
list(REMOVE_ITEM RECOMP_SOURCES "${UPSTREAM}/ps2xRecomp/src/lib/vu_translation_helpers.cpp")
list(APPEND RECOMP_SOURCES "${COLLECTION_ROOT}/third_party/ps2/recomp/vu_translation_helpers.cpp")
add_library(ps2_recomp_lib STATIC ${RECOMP_SOURCES})
target_compile_definitions(ps2_recomp_lib PUBLIC LIBDWARF_STATIC)
target_include_directories(ps2_recomp_lib PUBLIC
  "${UPSTREAM}/ps2xRecomp/include" "${DEPS}/elfio"
  "${DEPS}/libdwarf/src/lib/libdwarf" "${CMAKE_BINARY_DIR}/deps/libdwarf"
  "${UPSTREAM}/ps2xRuntime/include")
target_link_libraries(ps2_recomp_lib PUBLIC fmt::fmt toml11::toml11 dwarf ps2_rabbitizer)
file(GLOB RECOMP_RUNNER CONFIGURE_DEPENDS "${UPSTREAM}/ps2xRecomp/src/runner/*.cpp")
add_executable(ps2_recomp ${RECOMP_RUNNER})
target_link_libraries(ps2_recomp PRIVATE ps2_recomp_lib)

set(ANALYZER "${UPSTREAM}/ps2xAnalyzer/src")
add_library(ps2_analyzer_lib STATIC
  "${ANALYZER}/analysis_passes.cpp" "${ANALYZER}/elf_analysis_context.cpp"
  "${ANALYZER}/elf_analyzer.cpp" "${ANALYZER}/function_classifier.cpp"
  "${ANALYZER}/sce_symbol_scanner.cpp" "${ANALYZER}/toml_generator.cpp")
target_include_directories(ps2_analyzer_lib PUBLIC "${UPSTREAM}/ps2xAnalyzer/include")
target_link_libraries(ps2_analyzer_lib PUBLIC ps2_recomp_lib nlohmann_json::nlohmann_json)
add_executable(ps2_analyzer "${ANALYZER}/analyzer_main.cpp")
target_link_libraries(ps2_analyzer PRIVATE ps2_analyzer_lib)

enable_testing()
add_executable(ps2_generate_vu_fixture "${COLLECTION_ROOT}/tests/ps2_generate_vu_fixture.cpp")
target_link_libraries(ps2_generate_vu_fixture PRIVATE ps2_recomp_lib)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/include/ps2_vu_fixture.h"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/include"
  COMMAND ps2_generate_vu_fixture "${CMAKE_BINARY_DIR}/include/ps2_vu_fixture.h"
  DEPENDS ps2_generate_vu_fixture VERBATIM)
add_executable(ps2_vu_contract "${COLLECTION_ROOT}/tests/ps2_vu_contract.cpp"
  "${CMAKE_BINARY_DIR}/include/ps2_vu_fixture.h")
target_include_directories(ps2_vu_contract PRIVATE "${CMAKE_BINARY_DIR}/include")
target_compile_options(ps2_vu_contract PRIVATE -Werror=array-bounds)
add_test(NAME ps2_vu_contract COMMAND ps2_vu_contract)

option(PS2_BUILD_RUNTIME "Compile the initial shared PS2 runtime library" ON)
if(PS2_BUILD_RUNTIME)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  set(CUSTOMIZE_BUILD OFF CACHE BOOL "" FORCE)
  set(PS2X_IOP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${DEPS}/raylib" "deps/raylib" EXCLUDE_FROM_ALL)
  add_subdirectory("${UPSTREAM}/ps2xIOP" "deps/iop" EXCLUDE_FROM_ALL)
  set(RUNTIME "${UPSTREAM}/ps2xRuntime")
  file(GLOB_RECURSE KERNEL_SOURCES CONFIGURE_DEPENDS "${RUNTIME}/src/lib/Kernel/*.cpp")
  list(REMOVE_ITEM KERNEL_SOURCES "${RUNTIME}/src/lib/Kernel/EeScheduler.cpp")
  list(APPEND KERNEL_SOURCES "${COLLECTION_ROOT}/third_party/ps2/runtime/EeScheduler.cpp")
  list(REMOVE_ITEM KERNEL_SOURCES "${RUNTIME}/src/lib/Kernel/Syscalls/Thread.cpp")
  list(APPEND KERNEL_SOURCES "${COLLECTION_ROOT}/third_party/ps2/runtime/Thread.cpp")
  add_library(ps2_runtime STATIC
    "${RUNTIME}/src/lib/game_overrides.cpp"
    "${RUNTIME}/src/lib/gs/ps2_gif_arbiter.cpp"
    "${RUNTIME}/src/lib/ps2_audio.cpp"
    "${RUNTIME}/src/lib/ps2_audio_vag.cpp"
    "${RUNTIME}/src/lib/gs/ps2_gs_memory.cpp"
    "${RUNTIME}/src/lib/gs/gs_frontend.cpp"
    "${RUNTIME}/src/lib/gs/gs_cpu_backend.cpp"
    "${RUNTIME}/src/lib/ps2_iop_host.cpp"
    "${RUNTIME}/src/lib/ps2_memory.cpp"
    "${RUNTIME}/src/lib/ps2_pad.cpp"
    "${RUNTIME}/src/lib/ps2_rom_device.cpp"
    "${RUNTIME}/src/lib/ps2_vfs.cpp"
    "${RUNTIME}/src/lib/ps2_runtime.cpp"
    "${RUNTIME}/src/lib/ps2_vif1_interpreter.cpp"
    "${RUNTIME}/src/lib/vu/ps2_vu1_core.cpp"
    "${RUNTIME}/src/lib/vu/ps2_vu1_upper.cpp"
    "${RUNTIME}/src/lib/vu/ps2_vu1_lower.cpp"
    "${RUNTIME}/src/lib/games_database.cpp" ${KERNEL_SOURCES})
  target_include_directories(ps2_runtime PUBLIC "${RUNTIME}/include"
    "${RUNTIME}/src/lib/Kernel" PRIVATE "${RUNTIME}/src/lib"
    "${RUNTIME}/src/lib/Kernel/Syscalls")
  target_compile_definitions(ps2_runtime PRIVATE PS2X_HAS_FFMPEG=0 PS2X_ENABLE_IOP_RPC_TRACE=0)
  target_link_libraries(ps2_runtime PUBLIC raylib ps2_iop)

  add_executable(ps2_sdk_contract "${COLLECTION_ROOT}/tests/ps2_sdk_test.cpp")
  target_include_directories(ps2_sdk_contract PRIVATE "${COLLECTION_ROOT}")
  target_link_libraries(ps2_sdk_contract PRIVATE ps2_runtime)
  add_test(NAME ps2_sdk_contract COMMAND ps2_sdk_contract)

  add_executable(ps2_kernel_contract "${COLLECTION_ROOT}/tests/ps2_kernel_test.cpp"
    "${UPSTREAM}/ps2xTest/src/test_function_table.cpp")
  target_link_libraries(ps2_kernel_contract PRIVATE ps2_runtime)
  add_test(NAME ps2_kernel_contract COMMAND ps2_kernel_contract)

  set(PS2_GAME_GENERATED "" CACHE PATH "Private generated ESPGaluda directory for initial bring-up")
  if(PS2_GAME_GENERATED)
    file(GLOB GENERATED_SOURCES CONFIGURE_DEPENDS "${PS2_GAME_GENERATED}/*.cpp")
    if(NOT EXISTS "${PS2_GAME_GENERATED}/register_functions.cpp")
      message(FATAL_ERROR "Missing generated function table")
    endif()
    add_executable(ps2_espgaluda_probe "${COLLECTION_ROOT}/backends/ps2/probe_main.cpp"
      "${COLLECTION_ROOT}/games/espgaluda/sdk_bindings.cpp" ${GENERATED_SOURCES})
    target_include_directories(ps2_espgaluda_probe PRIVATE "${PS2_GAME_GENERATED}")
    target_link_libraries(ps2_espgaluda_probe PRIVATE ps2_runtime)
    target_compile_options(ps2_espgaluda_probe PRIVATE /bigobj)
    target_precompile_headers(ps2_espgaluda_probe PRIVATE
      "${RUNTIME}/include/ps2_runtime.h" "${PS2_GAME_GENERATED}/ps2_recompiled_functions.h"
      "${PS2_GAME_GENERATED}/ps2_recompiled_stubs.h")
    set_target_properties(ps2_espgaluda_probe PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 256)
    # The upstream runtime creates sizeable EE/VU stack frames.
    target_link_options(ps2_espgaluda_probe PRIVATE /STACK:8388608)
  endif()
endif()
