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
  if(prefix STREQUAL "ps2_daioujou_death")
    # Observe the original collision writes in this one program, without
    # adding a test to the memory path shared by all PS2 games.
    set(damage_source "${generated}/FUN_0023aae0_0x23aae0.cpp")
    set(damage_observer "${COLLECTION_ROOT}/games/daioujou-ps2/wire_gadget_damage.py")
    set(observed_damage "${CMAKE_CURRENT_BINARY_DIR}/death_label_damage.cpp")
    add_custom_command(OUTPUT "${observed_damage}"
      COMMAND py -3.12 "${damage_observer}" "${damage_source}" "${observed_damage}"
      DEPENDS "${damage_source}" "${damage_observer}" VERBATIM)
    list(FIND program_sources "${damage_source}" damage_index)
    if(damage_index LESS 0)
      message(FATAL_ERROR "Missing reviewed Death Label collision code")
    endif()
    list(INSERT program_sources ${damage_index} "${observed_damage}")
    list(REMOVE_ITEM program_sources "${damage_source}")
  endif()
  add_library(${prefix} OBJECT ${program_sources})
  target_include_directories(${prefix} PRIVATE "${generated}")
  target_link_libraries(${prefix} PRIVATE ps2_runtime)
  if(target STREQUAL "ps2_rtype_final_probe")
    target_compile_definitions(${prefix} PRIVATE EVAC_PS2_INLINE_RAM_ACCESSES=1)
    if(PS2_RUNTIME_THINLTO)
      target_compile_options(${prefix} PRIVATE /clang:-flto=thin)
    endif()
  endif()
  target_compile_options(${prefix} PRIVATE /bigobj /clang:-msse4.1)
  target_precompile_headers(${prefix} PRIVATE "${alias_header}"
    "${PS2_RUNTIME_INCLUDE}/ps2_runtime.h"
    "${generated}/ps2_recompiled_functions.h"
    "${generated}/ps2_recompiled_stubs.h")
  set_target_properties(${prefix} PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 256)
  target_sources(${target} PRIVATE $<TARGET_OBJECTS:${prefix}>)
endfunction()

set(PS2_RTYPE_FINAL_GENERATED "" CACHE PATH "Private generated R-Type Final native code")
set(PS2_RTYPE_FINAL_BANKS "" CACHE FILEPATH "Private R-Type Final code bank manifest")
set(PS2_RTYPE_FINAL_VU "" CACHE PATH "Private R-Type Final VU1 sources")
set(PS2_RTYPE_FINAL_FFMPEG "" CACHE PATH "Windows FFmpeg SDK for R-Type Final movie playback")
if(PS2_RTYPE_FINAL_GENERATED)
  if(NOT PS2_RTYPE_FINAL_BANKS OR NOT PS2_RTYPE_FINAL_VU OR NOT PS2_RTYPE_FINAL_FFMPEG)
    message(FATAL_ERROR "R-Type Final requires the reviewed banks, VU1 sources and movie decoder SDK")
  endif()
  add_executable(ps2_rtype_final_probe)
  ps2_configure_native_title(ps2_rtype_final_probe rtype-final "${PS2_RTYPE_FINAL_GENERATED}"
    EVAC_PS2_RTYPE_FINAL "${COLLECTION_ROOT}/games/rtype-final/sdk_bindings.cpp")
  set(bank_directory "${CMAKE_CURRENT_BINARY_DIR}/rtype-final-banks")
  set(bank_script "${COLLECTION_ROOT}/scripts/ps2_rtype_banks.py")
  execute_process(COMMAND py -3.12 "${bank_script}" "${PS2_RTYPE_FINAL_BANKS}" "${bank_directory}"
    RESULT_VARIABLE bank_result ERROR_VARIABLE bank_error)
  if(NOT bank_result EQUAL 0)
    message(FATAL_ERROR "Invalid R-Type Final code banks: ${bank_error}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${bank_script}" "${PS2_RTYPE_FINAL_BANKS}")
  include("${bank_directory}/modules.cmake")
  if(PS2_RUNTIME_THINLTO)
    target_compile_options(ps2_rtype_final_probe PRIVATE /clang:-flto=thin)
  endif()
  target_compile_definitions(ps2_rtype_final_probe PRIVATE EVAC_PS2_RTYPE_FINAL_BANKS=1 EVAC_PS2_INLINE_RAM_ACCESSES=1)

  file(GLOB rtype_vu_sources CONFIGURE_DEPENDS "${PS2_RTYPE_FINAL_VU}/*.cpp")
  if(NOT rtype_vu_sources)
    message(FATAL_ERROR "No compiled VU1 microprograms for R-Type Final")
  endif()
  add_library(ps2_rtype_final_vu OBJECT ${rtype_vu_sources})
  target_link_libraries(ps2_rtype_final_vu PRIVATE ps2_runtime)
  target_compile_options(ps2_rtype_final_vu PRIVATE /clang:-O3 /clang:-msse4.1 /clang:-g0)
  if(PS2_RUNTIME_THINLTO)
    target_compile_options(ps2_rtype_final_vu PRIVATE /clang:-flto=thin)
  endif()
  target_sources(ps2_rtype_final_probe PRIVATE $<TARGET_OBJECTS:ps2_rtype_final_vu>)

  # The original MPEG SDK calls delegate decoding to the host. Only R-Type
  # Final links FFmpeg. Its MPEG implementation resolves the runtime's
  # references before the disabled implementation in the static archive.
  set(movie_stubs "${RUNTIME}/src/lib/Kernel/Stubs")
  set(movie_overlay "${CMAKE_CURRENT_BINARY_DIR}/movie-overlay")
  set(movie_patch "${CMAKE_CURRENT_LIST_DIR}/movie-runtime.patch.txt")
  file(MAKE_DIRECTORY "${movie_overlay}")
  file(READ "${mpegWaitSource}" movie_source)
  file(WRITE "${movie_overlay}/MPEG.cpp" "${movie_source}")
  find_package(Git REQUIRED)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
    "${GIT_EXECUTABLE}" apply --no-index --whitespace=error "${movie_patch}"
    WORKING_DIRECTORY "${movie_overlay}" COMMAND_ERROR_IS_FATAL ANY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${movie_patch}" "${movie_stubs}/MPEG.cpp")
  file(COPY_FILE "${movie_overlay}/MPEG.cpp" "${CMAKE_CURRENT_BINARY_DIR}/rtype_movie.cpp" ONLY_IF_DIFFERENT)
  add_library(ps2_rtype_final_movie OBJECT "${CMAKE_CURRENT_BINARY_DIR}/rtype_movie.cpp")
  target_include_directories(ps2_rtype_final_movie PRIVATE "${movie_stubs}")
  target_link_libraries(ps2_rtype_final_movie PRIVATE ps2_runtime)
  target_include_directories(ps2_rtype_final_movie PRIVATE
    "${RUNTIME}/src/lib" "${PS2_RTYPE_FINAL_FFMPEG}/include")
  target_compile_definitions(ps2_rtype_final_movie PRIVATE PS2X_HAS_FFMPEG=1)
  target_sources(ps2_rtype_final_probe PRIVATE $<TARGET_OBJECTS:ps2_rtype_final_movie>)
  set_source_files_properties("${COLLECTION_ROOT}/games/rtype-final/sdk_bindings.cpp"
    PROPERTIES COMPILE_DEFINITIONS EVAC_PS2_RTYPE_FINAL_MOVIES=1 SKIP_PRECOMPILE_HEADERS ON)
  foreach(codec avcodec avutil swscale)
    target_link_libraries(ps2_rtype_final_probe PRIVATE "${PS2_RTYPE_FINAL_FFMPEG}/bin/${codec}.lib")
  endforeach()
  set(decoder_sources "")
  set(decoder_outputs "")
  foreach(dll avcodec-61.dll avutil-59.dll swscale-8.dll)
    list(APPEND decoder_sources "${PS2_RTYPE_FINAL_FFMPEG}/bin/${dll}")
    list(APPEND decoder_outputs "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${dll}")
  endforeach()
  add_custom_command(OUTPUT ${decoder_outputs}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${decoder_sources} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    DEPENDS ${decoder_sources} VERBATIM)
  add_custom_target(ps2_rtype_final_movie_runtime DEPENDS ${decoder_outputs})
  add_dependencies(ps2_rtype_final_probe ps2_rtype_final_movie_runtime)
  add_executable(ps2_movie_state_contract EXCLUDE_FROM_ALL "${COLLECTION_ROOT}/tests/ps2_movie_state_test.cpp"
    "${UPSTREAM}/ps2xTest/src/test_function_table.cpp")
  target_compile_definitions(ps2_movie_state_contract PRIVATE PS2X_HAS_FFMPEG=1)
  target_include_directories(ps2_movie_state_contract PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${movie_stubs}"
    "${RUNTIME}/src/lib" "${PS2_RTYPE_FINAL_FFMPEG}/include")
  target_link_libraries(ps2_movie_state_contract PRIVATE ps2_runtime
    "${PS2_RTYPE_FINAL_FFMPEG}/bin/avcodec.lib" "${PS2_RTYPE_FINAL_FFMPEG}/bin/avutil.lib"
    "${PS2_RTYPE_FINAL_FFMPEG}/bin/swscale.lib")
  add_dependencies(ps2_movie_state_contract ps2_rtype_final_movie_runtime)
  add_test(NAME ps2_movie_state_contract COMMAND ps2_movie_state_contract
    "${COLLECTION_ROOT}/tests/fixtures/ps2-mpeg-open-gop.m2v")
endif()

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
