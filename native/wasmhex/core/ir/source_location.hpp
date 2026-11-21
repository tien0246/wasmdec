// Strong-typed offsets for the three address spaces: SourceOffset (inside
// the original .wasm), VirtualOffset (inside the rewritten .text) and
// DatabaseEA (IDB effective address). Metadata netnodes store only offsets
// that survive a .text layout change; every translation between the spaces
// goes through the conversion API below.
#pragma once
#include <cstdint>

namespace wasmhex
{

struct SourceOffset
{
  std::uint32_t v = 0; // offset in the original wasm module
};

struct VirtualOffset
{
  std::uint32_t v = 0; // offset in the rewritten .text (stubs included)
};

struct DatabaseEA
{
  // Wide enough for ea_t in 32- and 64-bit-EA builds; core/ stays SDK-free,
  // IDA-facing code converts at the boundary.
  std::uint64_t v = 0;
};

// the one conversion API

inline SourceOffset to_source(VirtualOffset vo)
{
  return SourceOffset{ vo.v };
}

// Identity — see to_source.
inline VirtualOffset to_virtual(SourceOffset so)
{
  return VirtualOffset{ so.v };
}

// Virtual .text offset -> IDB address. Exact under the layout: the stub
// shift is already part of every VirtualOffset parse_code produces.
inline DatabaseEA to_ea(DatabaseEA func_start, VirtualOffset vo)
{
  return DatabaseEA{ func_start.v + vo.v };
}

} // namespace wasmhex
