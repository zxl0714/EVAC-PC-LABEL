# SPDX-FileCopyrightText: Copyright 2023 Citra Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
# Adapted from shadPS4 src/video_core/host_shaders/CMakeLists.txt at 529ceb32f5546b820588f194b903cc39d08015fe.

set(HOST_SHADER_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/upstream/src/video_core/host_shaders")
set(HOST_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/video_core/host_shaders")
function(ketsui_gpu_shader FILE SYMBOL)
    if(IS_ABSOLUTE "${FILE}")
        set(INPUT "${FILE}")
    else()
        set(INPUT "${HOST_SHADER_INPUT}/${FILE}")
    endif()
    set(OUTPUT "${HOST_SHADER_OUTPUT}/${SYMBOL}.h")
    set(DEFINES ${ARGN})
    list(TRANSFORM DEFINES PREPEND "-D")
    string(TOUPPER "${SYMBOL}" VARIABLE)
    add_custom_command(OUTPUT "${OUTPUT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${HOST_SHADER_OUTPUT}"
        COMMAND glslang-standalone --target-env vulkan1.3 --vn "${VARIABLE}"
            --depfile "${OUTPUT}.d" ${DEFINES} -o "${OUTPUT}" "${INPUT}"
        COMMAND "${CMAKE_COMMAND}" "-DHEADER_FILE=${OUTPUT}"
            -P "${HOST_SHADER_INPUT}/AddShaderHeaderInclude.cmake"
        DEPENDS glslang-standalone "${INPUT}"
            "${HOST_SHADER_INPUT}/AddShaderHeaderInclude.cmake"
        DEPFILE "${OUTPUT}.d" BYPRODUCTS "${OUTPUT}.d" VERBATIM)
    set(KETSUI_HOST_SHADER_HEADERS ${KETSUI_HOST_SHADER_HEADERS} "${OUTPUT}" PARENT_SCOPE)
endfunction()
ketsui_gpu_shader(color_to_ms_depth.frag color_to_ms_depth_frag)
ketsui_gpu_shader(fs_tri.vert fs_tri_vert)
ketsui_gpu_shader("${CMAKE_CURRENT_SOURCE_DIR}/host/present.frag" host_present_frag)
ketsui_gpu_shader(ms_image_blit.frag ms_image_blit_frag)
ketsui_gpu_shader(ms_image_blit.frag ms_image_blit_msaa_frag SRC_MSAA=1)
ketsui_gpu_shader(fault_buffer_process.comp fault_buffer_process_comp)
foreach(BPP IN ITEMS 8 16 32 64 96 128)
    ketsui_gpu_shader(tiling.comp tiling_micro_${BPP}_comp BITS_PER_PIXEL=${BPP} IS_MACRO_TILED=0)
    ketsui_gpu_shader(tiling.comp tiling_macro_${BPP}_comp BITS_PER_PIXEL=${BPP} IS_MACRO_TILED=1)
endforeach()
add_custom_target(KetsuiGpuHostShaders DEPENDS ${KETSUI_HOST_SHADER_HEADERS})
