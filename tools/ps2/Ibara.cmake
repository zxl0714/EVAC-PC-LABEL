# Native Ibara target, using the same runtime and generated-code ABI.
set(PS2_IBARA_GENERATED "" CACHE PATH "Private generated Ibara native code")
if(PS2_IBARA_GENERATED)
  if(NOT EXISTS "${PS2_IBARA_GENERATED}/register_functions.cpp")
    message(FATAL_ERROR "Missing generated Ibara function table")
  endif()
  file(GLOB IBARA_GENERATED_SOURCES CONFIGURE_DEPENDS "${PS2_IBARA_GENERATED}/*.cpp")
  add_executable(ps2_ibara_probe
    "${COLLECTION_ROOT}/backends/ps2/probe_main.cpp"
    "${COLLECTION_ROOT}/games/ibara/sdk_bindings.cpp" ${IBARA_GENERATED_SOURCES})
  target_include_directories(ps2_ibara_probe PRIVATE "${PS2_IBARA_GENERATED}")
  target_link_libraries(ps2_ibara_probe PRIVATE ps2_runtime)
  target_compile_definitions(ps2_ibara_probe PRIVATE EVAC_PS2_IBARA=1)
  # The pinned generator emits SSE4.1 blend/min/extract intrinsics for VU0.
  # Select that fixed minimum; never inherit the build machine's CPU features.
  target_compile_options(ps2_ibara_probe PRIVATE /bigobj /clang:-msse4.1)
  target_link_options(ps2_ibara_probe PRIVATE /STACK:8388608
    /MANIFEST:EMBED "/MANIFESTINPUT:${COLLECTION_ROOT}/common/windows.manifest")
  if(NOT PS2_GAME_SYMBOLS)
    target_link_options(ps2_ibara_probe PRIVATE /DEBUG:NONE)
  endif()
  target_precompile_headers(ps2_ibara_probe PRIVATE
    "${PS2_RUNTIME_INCLUDE}/ps2_runtime.h"
    "${PS2_IBARA_GENERATED}/ps2_recompiled_functions.h"
    "${PS2_IBARA_GENERATED}/ps2_recompiled_stubs.h")
  set_target_properties(ps2_ibara_probe PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 256)
endif()
