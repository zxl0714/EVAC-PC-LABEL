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
set(PS2_RUNTIME_INCLUDE "${CMAKE_BINARY_DIR}/runtime-include")
# All runtime headers come from one view, including quoted sibling includes in
# the SDK headers. Mixing the upstream class layout with the adapted layout
# would violate the native ABI.
file(COPY "${UPSTREAM}/ps2xRuntime/include/" DESTINATION "${PS2_RUNTIME_INCLUDE}"
  PATTERN "ps2_runtime.h" EXCLUDE PATTERN "ps2_memory.h" EXCLUDE
  PATTERN "gs_frontend.h" EXCLUDE PATTERN "ps2_gif_arbiter.h" EXCLUDE PATTERN "ee_scheduler.h" EXCLUDE)
configure_file("${COLLECTION_ROOT}/third_party/ps2/runtime/ps2_runtime.h"
  "${PS2_RUNTIME_INCLUDE}/ps2_runtime.h" COPYONLY)
configure_file("${COLLECTION_ROOT}/third_party/ps2/runtime/iop_subsystem.h"
  "${PS2_RUNTIME_INCLUDE}/ps2x/iop/iop_subsystem.h" COPYONLY)
configure_file("${COLLECTION_ROOT}/third_party/ps2/runtime/ee_scheduler.h"
  "${PS2_RUNTIME_INCLUDE}/runtime/ee_scheduler.h" COPYONLY)
configure_file("${COLLECTION_ROOT}/third_party/ps2/runtime/ps2_memory.h"
  "${PS2_RUNTIME_INCLUDE}/runtime/ps2_memory.h" COPYONLY)
foreach(header gs_frontend ps2_gif_arbiter)
  configure_file("${COLLECTION_ROOT}/third_party/ps2/runtime/${header}.h"
    "${PS2_RUNTIME_INCLUDE}/runtime/gs/${header}.h" COPYONLY)
endforeach()

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
list(REMOVE_ITEM RECOMP_SOURCES "${UPSTREAM}/ps2xRecomp/src/lib/fpu_translator.cpp")
list(APPEND RECOMP_SOURCES "${COLLECTION_ROOT}/third_party/ps2/recomp/fpu_translator.cpp")
add_library(ps2_recomp_lib STATIC ${RECOMP_SOURCES})
target_compile_definitions(ps2_recomp_lib PUBLIC LIBDWARF_STATIC)
target_include_directories(ps2_recomp_lib PUBLIC
  "${UPSTREAM}/ps2xRecomp/include" "${DEPS}/elfio"
  "${DEPS}/libdwarf/src/lib/libdwarf" "${CMAKE_BINARY_DIR}/deps/libdwarf"
  "${PS2_RUNTIME_INCLUDE}" "${COLLECTION_ROOT}/backends/ps2")
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
add_executable(ps2_module_contract "${COLLECTION_ROOT}/tests/ps2_modules_test.cpp"
  "${COLLECTION_ROOT}/backends/ps2/static_modules.cpp")
target_include_directories(ps2_module_contract PRIVATE "${COLLECTION_ROOT}/backends/ps2")
add_test(NAME ps2_module_contract COMMAND ps2_module_contract)
add_executable(ps2_iso_contract "${COLLECTION_ROOT}/tests/ps2_iso_test.cpp"
  "${COLLECTION_ROOT}/backends/ps2/iso_disc.cpp")
target_include_directories(ps2_iso_contract PRIVATE "${COLLECTION_ROOT}/backends/ps2")
add_test(NAME ps2_iso_contract COMMAND ps2_iso_contract)
add_executable(ps2_pad_contract "${COLLECTION_ROOT}/tests/ps2_pad_test.cpp"
  "${COLLECTION_ROOT}/third_party/ps2/runtime/ps2_pad.cpp")
target_include_directories(ps2_pad_contract PRIVATE "${PS2_RUNTIME_INCLUDE}" "${DEPS}/raylib/src")
add_test(NAME ps2_pad_contract COMMAND ps2_pad_contract)

option(PS2_BUILD_RUNTIME "Compile the initial shared PS2 runtime library" ON)
if(PS2_BUILD_RUNTIME)
  if(NOT PS2_GS_SOURCE)
    message(FATAL_ERROR "Prepare pinned GS sources with scripts/Build-Ps2.ps1")
  endif()
  add_subdirectory("${COLLECTION_ROOT}/tools/ps2-gs" "graphics" EXCLUDE_FROM_ALL)
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory("${DEPS}/SDL" "deps/sdl" EXCLUDE_FROM_ALL)
  add_library(ps2_shared_input STATIC
    "${COLLECTION_ROOT}/backends/ps4/input/sdl_gamepad.cpp"
    "${COLLECTION_ROOT}/backends/ps4/input/keyboard_config.cpp"
    "${COLLECTION_ROOT}/backends/ps4/community/protocol.cpp"
    "${COLLECTION_ROOT}/common/nickname.cpp")
  target_include_directories(ps2_shared_input PRIVATE "${COLLECTION_ROOT}")
  target_compile_definitions(ps2_shared_input PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(ps2_shared_input PUBLIC SDL3::SDL3 nlohmann_json::nlohmann_json PRIVATE winhttp bcrypt crypt32 normaliz)
  include("${CMAKE_CURRENT_LIST_DIR}/Spu.cmake")
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  set(CUSTOMIZE_BUILD OFF CACHE BOOL "" FORCE)
  set(PS2X_IOP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${DEPS}/raylib" "deps/raylib" EXCLUDE_FROM_ALL)
  add_subdirectory("${UPSTREAM}/ps2xIOP" "deps/iop" EXCLUDE_FROM_ALL)
  # Every private IOP include sees one memory layout, including quoted sibling
  # headers. Keep the pinned dependency tree immutable.
  set(IOP_SOURCE_VIEW "${CMAKE_BINARY_DIR}/iop-source")
  file(COPY "${UPSTREAM}/ps2xIOP/src/" DESTINATION "${IOP_SOURCE_VIEW}"
    PATTERN "iop_memory.h" EXCLUDE PATTERN "iop_memory.cpp" EXCLUDE
    PATTERN "iop_module_loader.cpp" EXCLUDE PATTERN "iop_emulator.cpp" EXCLUDE
    PATTERN "iop_emulator.h" EXCLUDE PATTERN "iop_subsystem.cpp" EXCLUDE
    PATTERN "iop_sysclib.cpp" EXCLUDE)
  configure_file("${COLLECTION_ROOT}/third_party/ps2/runtime/iop_memory.h"
    "${IOP_SOURCE_VIEW}/emulator/core/iop_memory.h" COPYONLY)
  configure_file("${COLLECTION_ROOT}/third_party/ps2/runtime/iop_emulator.h"
    "${IOP_SOURCE_VIEW}/emulator/iop_emulator.h" COPYONLY)
  configure_file("${COLLECTION_ROOT}/third_party/ps2/runtime/iop_subsystem.h"
    "${IOP_SOURCE_VIEW}/ps2x/iop/iop_subsystem.h" COPYONLY)
  get_target_property(IOP_SOURCES ps2_iop SOURCES)
  list(REMOVE_ITEM IOP_SOURCES "src/emulator/core/iop_memory.cpp" "src/emulator/services/iop_module_loader.cpp"
    "src/emulator/iop_emulator.cpp" "src/iop_subsystem.cpp" "src/emulator/imports/iop_sysclib.cpp")
  list(TRANSFORM IOP_SOURCES REPLACE "^src/" "${IOP_SOURCE_VIEW}/")
  set_property(TARGET ps2_iop PROPERTY SOURCES ${IOP_SOURCES}
    "${COLLECTION_ROOT}/third_party/ps2/runtime/iop_memory.cpp"
    "${COLLECTION_ROOT}/third_party/ps2/runtime/iop_module_loader.cpp"
    "${COLLECTION_ROOT}/third_party/ps2/runtime/iop_emulator.cpp"
    "${COLLECTION_ROOT}/third_party/ps2/runtime/iop_subsystem.cpp"
    "${COLLECTION_ROOT}/third_party/ps2/runtime/iop_sysclib.cpp")
  target_include_directories(ps2_iop BEFORE PRIVATE "${IOP_SOURCE_VIEW}")
  target_link_libraries(ps2_iop PRIVATE ps2_spu)
  add_executable(ps2_iop_memory_contract "${COLLECTION_ROOT}/tests/ps2_iop_memory_test.cpp")
  target_include_directories(ps2_iop_memory_contract PRIVATE "${IOP_SOURCE_VIEW}")
  target_link_libraries(ps2_iop_memory_contract PRIVATE ps2_iop)
  add_test(NAME ps2_iop_memory_contract COMMAND ps2_iop_memory_contract)
  set(RUNTIME "${UPSTREAM}/ps2xRuntime")
  file(GLOB_RECURSE KERNEL_SOURCES CONFIGURE_DEPENDS "${RUNTIME}/src/lib/Kernel/*.cpp")
  list(REMOVE_ITEM KERNEL_SOURCES "${RUNTIME}/src/lib/Kernel/EeScheduler.cpp")
  list(APPEND KERNEL_SOURCES "${COLLECTION_ROOT}/third_party/ps2/runtime/EeScheduler.cpp")
  list(REMOVE_ITEM KERNEL_SOURCES "${RUNTIME}/src/lib/Kernel/Syscalls/Thread.cpp")
  list(APPEND KERNEL_SOURCES "${COLLECTION_ROOT}/third_party/ps2/runtime/Thread.cpp")
  list(REMOVE_ITEM KERNEL_SOURCES "${RUNTIME}/src/lib/Kernel/Syscalls/System.cpp")
  list(APPEND KERNEL_SOURCES "${COLLECTION_ROOT}/third_party/ps2/runtime/System.cpp")
  list(REMOVE_ITEM KERNEL_SOURCES "${RUNTIME}/src/lib/Kernel/Stubs/GS.cpp")
  list(APPEND KERNEL_SOURCES "${COLLECTION_ROOT}/third_party/ps2/runtime/GS.cpp")
  list(REMOVE_ITEM KERNEL_SOURCES "${RUNTIME}/src/lib/Kernel/Stubs/CD.cpp")
  list(APPEND KERNEL_SOURCES "${COLLECTION_ROOT}/third_party/ps2/runtime/CD.cpp")
  add_library(ps2_runtime STATIC
    "${RUNTIME}/src/lib/game_overrides.cpp"
    "${COLLECTION_ROOT}/third_party/ps2/runtime/ps2_gif_arbiter.cpp"
    "${RUNTIME}/src/lib/ps2_audio.cpp"
    "${RUNTIME}/src/lib/ps2_audio_vag.cpp"
    "${RUNTIME}/src/lib/gs/ps2_gs_memory.cpp"
    "${COLLECTION_ROOT}/third_party/ps2/runtime/gs_frontend.cpp"
    "${RUNTIME}/src/lib/gs/gs_cpu_backend.cpp"
    "${COLLECTION_ROOT}/third_party/ps2/runtime/ps2_iop_host.cpp"
    "${COLLECTION_ROOT}/third_party/ps2/runtime/ps2_memory.cpp"
    "${COLLECTION_ROOT}/third_party/ps2/runtime/ps2_pad.cpp"
    "${RUNTIME}/src/lib/ps2_rom_device.cpp"
    "${RUNTIME}/src/lib/ps2_vfs.cpp"
    "${COLLECTION_ROOT}/third_party/ps2/runtime/ps2_runtime.cpp"
    "${COLLECTION_ROOT}/backends/ps2/static_modules.cpp"
    "${COLLECTION_ROOT}/backends/ps2/native_host.cpp"
    "${COLLECTION_ROOT}/backends/ps2/iso_disc.cpp"
    "${COLLECTION_ROOT}/third_party/ps2/runtime/ps2_vif1_interpreter.cpp"
    "${COLLECTION_ROOT}/third_party/ps2/runtime/ps2_vu1_core.cpp"
    "${RUNTIME}/src/lib/vu/ps2_vu1_upper.cpp"
    "${RUNTIME}/src/lib/vu/ps2_vu1_lower.cpp"
    "${RUNTIME}/src/lib/games_database.cpp" ${KERNEL_SOURCES})
  target_include_directories(ps2_runtime PUBLIC "${PS2_RUNTIME_INCLUDE}"
    "${COLLECTION_ROOT}/backends/ps2"
    "${RUNTIME}/src/lib/Kernel" PRIVATE "${RUNTIME}/src/lib"
    "${RUNTIME}/src/lib/Kernel/Syscalls")
  target_compile_definitions(ps2_runtime PRIVATE PS2X_HAS_FFMPEG=0 PS2X_ENABLE_IOP_RPC_TRACE=0)
  add_subdirectory("${COLLECTION_ROOT}/common" "collection-common" EXCLUDE_FROM_ALL)
  target_include_directories(ps2_runtime PRIVATE "${COLLECTION_ROOT}")
  target_link_libraries(ps2_runtime PUBLIC raylib ps2_iop ps2_gs_device ps2_shared_input PRIVATE cave_common)
  add_library(ps2_collection STATIC "${COLLECTION_ROOT}/backends/ps2/collection_launch.cpp")
  target_include_directories(ps2_collection PRIVATE "${COLLECTION_ROOT}")
  target_link_libraries(ps2_collection PRIVATE cave_common dbghelp)

  add_executable(ps2_native_host_probe "${COLLECTION_ROOT}/tests/ps2_native_host_probe.cpp"
    "${UPSTREAM}/ps2xTest/src/test_function_table.cpp")
  target_link_libraries(ps2_native_host_probe PRIVATE ps2_runtime)
  target_link_options(ps2_native_host_probe PRIVATE /STACK:8388608)

  add_executable(ps2_sdk_contract "${COLLECTION_ROOT}/tests/ps2_sdk_test.cpp")
  target_include_directories(ps2_sdk_contract PRIVATE "${COLLECTION_ROOT}")
  target_link_libraries(ps2_sdk_contract PRIVATE ps2_runtime)
  add_test(NAME ps2_sdk_contract COMMAND ps2_sdk_contract)

  add_executable(ps2_kernel_contract "${COLLECTION_ROOT}/tests/ps2_kernel_test.cpp"
    "${UPSTREAM}/ps2xTest/src/test_function_table.cpp")
  target_link_libraries(ps2_kernel_contract PRIVATE ps2_runtime)
  add_test(NAME ps2_kernel_contract COMMAND ps2_kernel_contract)

  set(sdkFixture "${CMAKE_BINARY_DIR}/sdk-direct-fixture")
  set(sdkFixtureSources "${sdkFixture}/generated/FUN_00100000_0x100000.cpp"
    "${sdkFixture}/generated/FUN_00100100_0x100100.cpp")
  add_custom_command(OUTPUT ${sdkFixtureSources} "${sdkFixture}/generated/ps2_recompiled_functions.h"
    COMMAND py -3.12 "${COLLECTION_ROOT}/tests/ps2_generate_sdk_fixture.py"
      --tool $<TARGET_FILE:ps2_recomp> --output "${sdkFixture}"
    DEPENDS ps2_recomp "${COLLECTION_ROOT}/tests/ps2_generate_sdk_fixture.py"
      "${COLLECTION_ROOT}/scripts/ps2_codegen.py" VERBATIM)
  add_executable(ps2_sdk_codegen_contract "${COLLECTION_ROOT}/tests/ps2_sdk_codegen_test.cpp" ${sdkFixtureSources})
  target_include_directories(ps2_sdk_codegen_contract PRIVATE "${sdkFixture}/generated")
  target_link_libraries(ps2_sdk_codegen_contract PRIVATE ps2_runtime)
  add_test(NAME ps2_sdk_codegen_contract COMMAND ps2_sdk_codegen_contract)

  set(PS2_GAME_GENERATED "" CACHE PATH "Private generated ESPGaluda directory for initial bring-up")
  option(PS2_GAME_SYMBOLS "Link full PDBs for generated game hosts" ON)
  set(PS2_ARCADE_GENERATED "" CACHE PATH "Private generated ESPGaluda ARCADE module")
  set(PS2_ARRANGE_GENERATED "" CACHE PATH "Private generated ESPGaluda ARRANGE module")
  set(PS2_STAGE_MODULES_MANIFEST "" CACHE FILEPATH "Private stage module build manifest")
  set(PS2_MODE_EXTRAS_MANIFEST "" CACHE FILEPATH "Verified additional mode entry build manifest")
  if(PS2_GAME_GENERATED)
    file(GLOB GENERATED_SOURCES CONFIGURE_DEPENDS "${PS2_GAME_GENERATED}/*.cpp")
    if(NOT EXISTS "${PS2_GAME_GENERATED}/register_functions.cpp")
      message(FATAL_ERROR "Missing generated function table")
    endif()
    add_executable(ps2_espgaluda_probe "${COLLECTION_ROOT}/backends/ps2/probe_main.cpp"
      "${COLLECTION_ROOT}/games/espgaluda/sdk_bindings.cpp" ${GENERATED_SOURCES})
    target_include_directories(ps2_espgaluda_probe PRIVATE "${PS2_GAME_GENERATED}")
    target_link_libraries(ps2_espgaluda_probe PRIVATE ps2_runtime ps2_collection)
    set_target_properties(ps2_espgaluda_probe PROPERTIES OUTPUT_NAME "espgaluda" PDB_NAME "espgaluda")
    target_compile_options(ps2_espgaluda_probe PRIVATE /bigobj)
    target_precompile_headers(ps2_espgaluda_probe PRIVATE
      "${PS2_RUNTIME_INCLUDE}/ps2_runtime.h" "${PS2_GAME_GENERATED}/ps2_recompiled_functions.h"
      "${PS2_GAME_GENERATED}/ps2_recompiled_stubs.h")
    set_target_properties(ps2_espgaluda_probe PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 256)
    # The upstream runtime creates sizeable EE/VU stack frames.
    target_link_options(ps2_espgaluda_probe PRIVATE /STACK:8388608
      /MANIFEST:EMBED "/MANIFESTINPUT:${COLLECTION_ROOT}/common/windows.manifest")
    if(NOT PS2_GAME_SYMBOLS)
      target_link_options(ps2_espgaluda_probe PRIVATE /DEBUG:NONE)
    endif()
    if(PS2_ARCADE_GENERATED OR PS2_ARRANGE_GENERATED)
      if(NOT PS2_ARCADE_GENERATED OR NOT PS2_ARRANGE_GENERATED)
        message(FATAL_ERROR "Both ESPGaluda module banks are required")
      endif()
      foreach(module arcade arrange)
        string(TOUPPER "${module}" moduleUpper)
        if(module STREQUAL "arcade")
          set(tablePrefix "g_ps2ArcadeFunctionTable")
        else()
          set(tablePrefix "g_ps2ArrangeFunctionTable")
        endif()
        set(moduleDir "${PS2_${moduleUpper}_GENERATED}")
        if(NOT EXISTS "${moduleDir}/register_functions.cpp")
          message(FATAL_ERROR "Missing ESPGaluda ${module} function table")
        endif()
        file(GLOB moduleSources CONFIGURE_DEPENDS "${moduleDir}/*.cpp")
        add_library(ps2_espgaluda_${module} OBJECT ${moduleSources})
        target_include_directories(ps2_espgaluda_${module} PRIVATE "${moduleDir}")
        target_link_libraries(ps2_espgaluda_${module} PRIVATE ps2_runtime)
        target_compile_definitions(ps2_espgaluda_${module} PRIVATE
          "g_ps2RecompiledFunctionTable=${tablePrefix}"
          "g_ps2RecompiledFunctionTableBase=${tablePrefix}Base"
          "g_ps2RecompiledFunctionTableEnd=${tablePrefix}End"
          "g_ps2RecompiledFunctionTableSlotCount=${tablePrefix}SlotCount")
        target_compile_options(ps2_espgaluda_${module} PRIVATE /bigobj)
        target_precompile_headers(ps2_espgaluda_${module} PRIVATE
          "${PS2_RUNTIME_INCLUDE}/ps2_runtime.h" "${moduleDir}/ps2_recompiled_functions.h"
          "${moduleDir}/ps2_recompiled_stubs.h")
        set_target_properties(ps2_espgaluda_${module} PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 256)
        target_sources(ps2_espgaluda_probe PRIVATE $<TARGET_OBJECTS:ps2_espgaluda_${module}>)
      endforeach()
      # Older private analysis images used unscoped ELF root names. Isolate
      # their tiny entries and table with bank-specific compiler aliases,
      # preserving already compiled body objects. New analysis images scope
      # ELF roots by module id before generation and need no aliases.
      file(STRINGS "${PS2_ARCADE_GENERATED}/ps2_recompiled_functions.h" arcadeRoots REGEX "^void root_")
      file(STRINGS "${PS2_ARRANGE_GENERATED}/ps2_recompiled_functions.h" arrangeRoots REGEX "^void root_")
      set(arrangeAliases)
      set(arrangeAliasSources "${PS2_ARRANGE_GENERATED}/register_functions.cpp")
      foreach(declaration IN LISTS arrangeRoots)
        if(declaration IN_LIST arcadeRoots)
          string(REGEX MATCH "^void ([A-Za-z_0-9]+)\\(" rootMatch "${declaration}")
          set(rootSymbol "${CMAKE_MATCH_1}")
          if(NOT rootSymbol OR NOT EXISTS "${PS2_ARRANGE_GENERATED}/${rootSymbol}.cpp")
            message(FATAL_ERROR "Cannot isolate shared PS2 root: ${declaration}")
          endif()
          list(APPEND arrangeAliases "${rootSymbol}=espgaluda_arrange_${rootSymbol}")
          list(APPEND arrangeAliasSources "${PS2_ARRANGE_GENERATED}/${rootSymbol}.cpp")
        endif()
      endforeach()
      if(arrangeAliases)
        set_source_files_properties(${arrangeAliasSources} PROPERTIES
          COMPILE_DEFINITIONS "${arrangeAliases}" SKIP_PRECOMPILE_HEADERS ON)
      endif()
      target_sources(ps2_espgaluda_probe PRIVATE "${COLLECTION_ROOT}/games/espgaluda/static_modules.cpp")
      target_compile_definitions(ps2_espgaluda_probe PRIVATE EVAC_PS2_ESPGALUDA_MODULES=1)
      if(PS2_STAGE_MODULES_MANIFEST)
        set(stageBuildDir "${CMAKE_BINARY_DIR}/espgaluda-stages")
        execute_process(COMMAND py -3.12 "${COLLECTION_ROOT}/scripts/ps2_module_build.py"
          "${PS2_STAGE_MODULES_MANIFEST}" --metadata "${COLLECTION_ROOT}/games/espgaluda/modules.json"
          --output "${stageBuildDir}" RESULT_VARIABLE stageResult
          OUTPUT_VARIABLE stageOutput ERROR_VARIABLE stageErrors)
        if(NOT stageResult EQUAL 0)
          message(FATAL_ERROR "Invalid ESPGaluda stage input: ${stageErrors}")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
          "${PS2_STAGE_MODULES_MANIFEST}" "${COLLECTION_ROOT}/games/espgaluda/modules.json"
          "${COLLECTION_ROOT}/scripts/ps2_module_build.py"
          "${COLLECTION_ROOT}/scripts/ps2_translation_units.py")
        include("${stageBuildDir}/modules.cmake")
      endif()
      if(PS2_MODE_EXTRAS_MANIFEST)
        set(extrasBuildDir "${CMAKE_BINARY_DIR}/espgaluda-extras")
        execute_process(COMMAND py -3.12 "${COLLECTION_ROOT}/scripts/ps2_module_extras.py"
          "${PS2_MODE_EXTRAS_MANIFEST}" --output "${extrasBuildDir}"
          RESULT_VARIABLE extrasResult OUTPUT_VARIABLE extrasOutput ERROR_VARIABLE extrasErrors)
        if(NOT extrasResult EQUAL 0)
          message(FATAL_ERROR "Invalid PS2 additional functions: ${extrasErrors}")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
          "${PS2_MODE_EXTRAS_MANIFEST}" "${COLLECTION_ROOT}/scripts/ps2_module_extras.py")
        include("${extrasBuildDir}/extras.cmake")
      endif()
    endif()
  endif()
  include("${CMAKE_CURRENT_LIST_DIR}/Ibara.cmake")
endif()
