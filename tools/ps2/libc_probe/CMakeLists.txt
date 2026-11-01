cmake_minimum_required(VERSION 3.25)
project(Ps2LibraryProbe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
get_filename_component(COLLECTION_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(PS2_LIBC_GENERATED "" CACHE PATH "Private ESPGaluda translated library directory")
if(NOT EXISTS "${PS2_LIBC_GENERATED}/ps2_recompiled_functions.h")
  message(FATAL_ERROR "Set PS2_LIBC_GENERATED to an existing ESPGaluda generation")
endif()
set(runtime "${COLLECTION_ROOT}/out/ps2-tools")
add_executable(ps2_libc_probe "${COLLECTION_ROOT}/tests/ps2_libc_probe.cpp"
  "${PS2_LIBC_GENERATED}/FUN_00118048_0x118048.cpp"
  "${PS2_LIBC_GENERATED}/FUN_00119c70_0x119c70.cpp"
  "${PS2_LIBC_GENERATED}/FUN_00119ee8_0x119ee8.cpp"
  "${PS2_LIBC_GENERATED}/FUN_0011a000_0x11a000.cpp")
target_include_directories(ps2_libc_probe PRIVATE "${runtime}/runtime-include"
  "${COLLECTION_ROOT}/backends/ps2" "${PS2_LIBC_GENERATED}"
  "${COLLECTION_ROOT}/.deps/ps2/PS2Recomp/ps2xIOP/include"
  "${COLLECTION_ROOT}/.deps/ps2/PS2Recomp/ps2xRuntime/src/lib/Kernel"
  "${COLLECTION_ROOT}/.deps/ps2/raylib/src")
target_link_libraries(ps2_libc_probe PRIVATE "${runtime}/ps2_runtime.lib"
  "${runtime}/deps/iop/ps2_iop.lib" "${runtime}/deps/raylib/raylib/raylib.lib"
  opengl32 glu32 winmm user32 gdi32 shell32)
