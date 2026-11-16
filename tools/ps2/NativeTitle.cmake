# Native titles share the same toolchain and reviewed table layout.
function(ps2_configure_native_title target output generated definition)
  if(NOT EXISTS "${generated}/register_functions.cpp")
    message(FATAL_ERROR "Missing generated ${output} function table")
  endif()
  file(GLOB title_sources CONFIGURE_DEPENDS "${generated}/*.cpp")
  ps2_use_table_view("${generated}" title_sources)
  target_sources(${target} PRIVATE
    "${COLLECTION_ROOT}/backends/ps2/probe_main.cpp"
    ${ARGN} ${title_sources})
  target_include_directories(${target} PRIVATE "${generated}")
  target_link_libraries(${target} PRIVATE ps2_runtime ps2_collection)
  target_compile_definitions(${target} PRIVATE ${definition}=1)
  # The pinned generator emits SSE4.1 blend/min/extract intrinsics for VU0.
  # Select that fixed minimum; never inherit the build machine's CPU features.
  target_compile_options(${target} PRIVATE /bigobj /clang:-msse4.1)
  target_link_options(${target} PRIVATE /STACK:8388608
    /MANIFEST:EMBED "/MANIFESTINPUT:${COLLECTION_ROOT}/common/windows.manifest")
  if(NOT PS2_GAME_SYMBOLS)
    target_link_options(${target} PRIVATE /DEBUG:NONE)
  endif()
  target_precompile_headers(${target} PRIVATE
    "${PS2_RUNTIME_INCLUDE}/ps2_runtime.h"
    "${generated}/ps2_recompiled_functions.h"
    "${generated}/ps2_recompiled_stubs.h")
  set_target_properties(${target} PROPERTIES OUTPUT_NAME ${output} PDB_NAME ${output}
    UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 256)
endfunction()

set(PS2_IBARA_GENERATED "" CACHE PATH "Private generated Ibara native code")
if(PS2_IBARA_GENERATED)
  add_executable(ps2_ibara_probe)
  ps2_configure_native_title(ps2_ibara_probe ibara "${PS2_IBARA_GENERATED}" EVAC_PS2_IBARA
    "${COLLECTION_ROOT}/games/ibara/sdk_bindings.cpp")
endif()

set(PS2_DAIOUJOU_GENERATED "" CACHE PATH "Private generated DaiOuJou PS2 native code")
set(PS2_DAIOUJOU_DEATH_GENERATED "" CACHE PATH "Private generated DaiOuJou PS2 Death Label code")
set(PS2_DAIOUJOU_GALLERY_GENERATED "" CACHE PATH "Private generated DaiOuJou PS2 Gallery code")

function(ps2_add_disc_program target prefix generated)
  set(alias_header "${CMAKE_CURRENT_BINARY_DIR}/${prefix}_symbols.h")
  set(alias_script "${COLLECTION_ROOT}/scripts/ps2_program_symbols.py")
  execute_process(COMMAND py -3.12 "${alias_script}" "${generated}" "${prefix}" "${alias_header}"
    RESULT_VARIABLE alias_result ERROR_VARIABLE alias_error)
  if(NOT alias_result EQUAL 0)
    message(FATAL_ERROR "Cannot scope PS2 program ${prefix}: ${alias_error}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${alias_script}"
    "${generated}/ps2_recompiled_functions.h" "${generated}/ps2_recompiled_stubs.h")
  file(GLOB program_sources CONFIGURE_DEPENDS "${generated}/*.cpp")
  ps2_use_table_view("${generated}" program_sources)
  add_library(${prefix} OBJECT ${program_sources})
  target_include_directories(${prefix} PRIVATE "${generated}")
  target_link_libraries(${prefix} PRIVATE ps2_runtime)
  target_compile_options(${prefix} PRIVATE /bigobj /clang:-msse4.1)
  target_precompile_headers(${prefix} PRIVATE "${alias_header}"
    "${PS2_RUNTIME_INCLUDE}/ps2_runtime.h"
    "${generated}/ps2_recompiled_functions.h"
    "${generated}/ps2_recompiled_stubs.h")
  set_target_properties(${prefix} PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 256)
  target_sources(${target} PRIVATE $<TARGET_OBJECTS:${prefix}>)
endfunction()

if(PS2_DAIOUJOU_GENERATED)
  if(NOT PS2_DAIOUJOU_DEATH_GENERATED OR NOT PS2_DAIOUJOU_GALLERY_GENERATED)
    message(FATAL_ERROR "DaiOuJou PS2 requires the main, Death Label and Gallery programs")
  endif()
  add_library(ps2_daioujou_runtime SHARED)
  ps2_configure_native_title(ps2_daioujou_runtime daioujou-ps2-runtime "${PS2_DAIOUJOU_GENERATED}" EVAC_PS2_DAIOUJOU
    "${COLLECTION_ROOT}/games/daioujou-ps2/sdk_bindings.cpp"
    "${COLLECTION_ROOT}/games/daioujou-ps2/hosted.cpp")
  if(PS2_GAME_SYMBOLS)
    target_link_options(ps2_daioujou_runtime PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)
  endif()
  ps2_add_disc_program(ps2_daioujou_runtime ps2_daioujou_death "${PS2_DAIOUJOU_DEATH_GENERATED}")
  ps2_add_disc_program(ps2_daioujou_runtime ps2_daioujou_gallery "${PS2_DAIOUJOU_GALLERY_GENERATED}")
  add_executable(ps2_daioujou_probe "${COLLECTION_ROOT}/games/daioujou-ps2/main.cpp")
  target_link_libraries(ps2_daioujou_probe PRIVATE ps2_daioujou_runtime)
  target_link_options(ps2_daioujou_probe PRIVATE /STACK:8388608
    /MANIFEST:EMBED "/MANIFESTINPUT:${COLLECTION_ROOT}/common/windows.manifest")
  set_target_properties(ps2_daioujou_probe PROPERTIES OUTPUT_NAME daioujou-ps2 PDB_NAME daioujou-ps2)
endif()
