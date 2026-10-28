# cl.exe emits UTF-8 diagnostics on this host. CMake's compiler detection used
# the legacy Windows code page and cached a mojibake prefix, dropping headers.
set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:")
set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:")
set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:")
