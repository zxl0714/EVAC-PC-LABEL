#pragma once
#include "preparation_io.h"

namespace cave::ps4 {
// Plain, uncompressed SELF only. Does not decrypt or verify a signature.
PreparationBytes PlainSelfElf(std::span<const std::uint8_t> source);
}
