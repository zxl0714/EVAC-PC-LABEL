# A small pinned DSP component; no Play! CPU, graphics or frontend targets.
set(PS2_SPU_SOURCE "${CMAKE_BINARY_DIR}/spu-source")
execute_process(COMMAND py -3.12 "${COLLECTION_ROOT}/scripts/ps2_spu_sources.py"
  --dependencies "${DEPS}" --output "${PS2_SPU_SOURCE}"
  RESULT_VARIABLE spuPrepareResult OUTPUT_VARIABLE spuPrepareOutput ERROR_VARIABLE spuPrepareError)
if(NOT spuPrepareResult EQUAL 0)
  message(FATAL_ERROR "PS2 SPU source preparation failed: ${spuPrepareError}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${COLLECTION_ROOT}/scripts/ps2_spu_sources.py"
  "${COLLECTION_ROOT}/third_party/ps2/spu/sources.json")
add_library(ps2_spu STATIC
  "${PS2_SPU_SOURCE}/src/Iop_SpuBase.cpp"
  "${PS2_SPU_SOURCE}/src/Iop_Spu2_Core.cpp"
  "${PS2_SPU_SOURCE}/src/Iop_Spu2.cpp"
  "${COLLECTION_ROOT}/backends/ps2/spu_device.cpp")
target_include_directories(ps2_spu PUBLIC "${COLLECTION_ROOT}/backends/ps2"
  PRIVATE "${PS2_SPU_SOURCE}/include")
target_compile_definitions(ps2_spu PRIVATE LOGGING_ENABLED=0)
target_compile_options(ps2_spu PRIVATE /clang:-fno-strict-aliasing)
add_executable(ps2_spu_contract "${COLLECTION_ROOT}/tests/ps2_spu_test.cpp")
target_link_libraries(ps2_spu_contract PRIVATE ps2_spu)
add_test(NAME ps2_spu_contract COMMAND ps2_spu_contract)
