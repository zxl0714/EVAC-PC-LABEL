# Decoder sources from upstream v0.3.0, including its pinned codec implementations.
file(GLOB codecs CONFIGURE_DEPENDS src/libchdr_*.c)
add_library(cave_chdr STATIC ${codecs}
  deps/lzma-25.01/src/LzmaDec.c deps/miniz-3.1.1/miniz.c deps/zstd-1.5.7/zstddeclib.c)
target_include_directories(cave_chdr PUBLIC include)
target_compile_definitions(cave_chdr PRIVATE MINIZ_NO_ARCHIVE_APIS MINIZ_NO_DEFLATE_APIS
  MINIZ_NO_STDIO MINIZ_NO_TIME _CRT_SECURE_NO_WARNINGS)
