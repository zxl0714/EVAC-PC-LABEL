get_filename_component(CAVE_COMMON_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
file(READ "${CAVE_COMMON_ROOT}/config/default.ini" CAVE_DEFAULT_INI)
configure_file(default_config.h.in "${CMAKE_BINARY_DIR}/include/default_config.h" @ONLY)
set(CATALOGUE "${CAVE_COMMON_ROOT}/games/catalogue.json")
file(GLOB_RECURSE DATA_MANIFESTS CONFIGURE_DEPENDS "${CAVE_COMMON_ROOT}/games/*/data.json")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/include/catalogue_generated.h"
  COMMAND "${Python3_EXECUTABLE}" "${CAVE_COMMON_ROOT}/scripts/generate_catalogue.py"
    "${CATALOGUE}" "${CMAKE_BINARY_DIR}/include/catalogue_generated.h"
  DEPENDS "${CATALOGUE}" ${DATA_MANIFESTS} "${CAVE_COMMON_ROOT}/scripts/generate_catalogue.py" VERBATIM)
add_library(cave_common STATIC catalogue.cpp ini.cpp paths.cpp settings.cpp config_update.cpp launch.cpp data_import.cpp
  "${CMAKE_BINARY_DIR}/include/catalogue_generated.h")
target_compile_features(cave_common PUBLIC cxx_std_23)
target_include_directories(cave_common PUBLIC "${CAVE_COMMON_ROOT}" PRIVATE "${CMAKE_BINARY_DIR}/include")
target_compile_definitions(cave_common PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(cave_common PUBLIC bcrypt)
if(MSVC)
  target_compile_options(cave_common PUBLIC /utf-8 /W4)
else()
  target_compile_options(cave_common PUBLIC -Wall -Wextra -Wpedantic)
endif()
