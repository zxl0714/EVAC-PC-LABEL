# Compose the PS2 vector unit from the pinned PS2Recomp fork. The patch
# preserves standalone buffer replacement and enables SSE4.1 native blocks.
set(vu_overlay "${CMAKE_BINARY_DIR}/vu-overlay")
set(vu_patch "${CMAKE_CURRENT_LIST_DIR}/vu-runtime.patch.txt")
set(vu_headers ps2_vu1.h ps2_vu1_inline.h ps2_vu1_jit.h ps2_vu1_lifted.h ps2_vu_clip.h)
set(vu_core_path "ps2xRuntime/src/lib/vu/ps2_vu1_core.cpp")
set(vu_inputs "${vu_core_path}")
foreach(header IN LISTS vu_headers)
  list(APPEND vu_inputs "ps2xRuntime/include/runtime/${header}")
endforeach()
foreach(input IN LISTS vu_inputs)
  get_filename_component(parent "${vu_overlay}/${input}" DIRECTORY)
  file(MAKE_DIRECTORY "${parent}")
  file(READ "${VU_SOURCE}/${input}" vu_text)
  file(WRITE "${vu_overlay}/${input}" "${vu_text}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VU_SOURCE}/${input}")
endforeach()
find_package(Git REQUIRED)
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
    "${GIT_EXECUTABLE}" apply --no-index --whitespace=error "${vu_patch}"
  WORKING_DIRECTORY "${vu_overlay}" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${vu_patch}")
foreach(header IN LISTS vu_headers)
  file(COPY_FILE "${vu_overlay}/ps2xRuntime/include/runtime/${header}"
    "${PS2_RUNTIME_INCLUDE}/runtime/${header}" ONLY_IF_DIFFERENT)
endforeach()
set(PS2_VU_CORE "${CMAKE_BINARY_DIR}/ps2_vu1_core.cpp")
file(COPY_FILE "${vu_overlay}/${vu_core_path}" "${PS2_VU_CORE}" ONLY_IF_DIFFERENT)
