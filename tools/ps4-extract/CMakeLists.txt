add_library(ps4_extract_core STATIC extract.cpp)
target_include_directories(ps4_extract_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(ps4_extract_core PUBLIC UNICODE _UNICODE)
add_executable(ps4_extract WIN32 main.cpp)
set_target_properties(ps4_extract PROPERTIES OUTPUT_NAME "Extract-PS4")
target_link_libraries(ps4_extract PRIVATE ps4_extract_core comctl32 ole32 shell32 uuid)
target_link_options(ps4_extract PRIVATE "LINKER:/MANIFEST:EMBED"
  "LINKER:/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/common/windows.manifest"
  "LINKER:/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/windows.manifest")
add_executable(ps4_extract_tests ../../tests/ps4_extract_test.cpp)
target_link_libraries(ps4_extract_tests PRIVATE ps4_extract_core)
add_test(NAME ps4_extract COMMAND ps4_extract_tests)
set_tests_properties(ps4_extract PROPERTIES TIMEOUT 45)
# This small standalone player tool must also work when launched from tools/,
# without searching the root launcher's app-local C++ DLLs.
foreach(target ps4_extract_core ps4_extract ps4_extract_tests)
  set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endforeach()
