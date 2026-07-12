#pragma once

#include <cstddef>

namespace cds {

#ifdef ENABLE_PADDING
// Hardcoded rather than std::hardware_destructive_interference_size: that
// constant is ABI-frozen at 64 on many toolchains (incl. Apple Clang/libc++)
// even on Apple Silicon, whose real line is 128. Padding to 64 there leaves
// head_/tail_ on the same 128B line -- the false sharing this is meant to
// remove. 128 is also the safe floor on x86-64 (adjacent-line prefetch pairs
// two 64B lines).
inline constexpr std::size_t cache_line_size = 128;
#define CACHE_ALIGNED alignas(::cds::cache_line_size)
#else
#define CACHE_ALIGNED
#endif

} // namespace cds
