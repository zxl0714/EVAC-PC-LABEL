# Platform components contain no game addresses, translated bodies or mode list.
add_library(cave_xbox360_input STATIC input/configured_input.cpp input/input_state.cpp)
target_include_directories(cave_xbox360_input PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cave_xbox360_input PUBLIC cave_common rex::runtime)

add_library(cave_community_http STATIC ${CMAKE_SOURCE_DIR}/common/community/http_client.cpp)
target_include_directories(cave_community_http PUBLIC "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/common"
    "${CMAKE_SOURCE_DIR}/third_party/rexglue/thirdparty/inja/third_party/include")
target_link_libraries(cave_community_http PUBLIC rex::runtime winhttp normaliz)
