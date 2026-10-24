# Every mode occupies the same guest range but owns an independent native map.
function(guwange_add_overlay name)
    include("${CMAKE_SOURCE_DIR}/generated/guwange/${name}/sources.cmake")
    add_library(guwange_${name} SHARED ${GENERATED_SOURCES})
    target_include_directories(guwange_${name} PRIVATE
        "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/generated/guwange/${name}")
    target_compile_options(guwange_${name} PRIVATE -fno-char8_t)
    target_link_libraries(guwange_${name} PRIVATE rex::runtime)
    rexglue_configure_module_target(guwange_${name} HOST guwange)
endfunction()
foreach(mode Guwange01_x360 Guwange01_coop_x360 Guwange_arrange_x360 Guwange_blue_x360)
    guwange_add_overlay(${mode})
endforeach()
