// LEB128 primitives shared by every binary-layer consumer (split
// out of core/types.hpp; types.hpp re-includes this header as a shim so
// existing include sites keep compiling).
#pragma once

#include <cstdint>

namespace wasmhex::loader
{

// Reads one signed LEB128 value of at most 33 payload bits. Like a streaming
// reader, *pos advances as bytes are consumed and is left where reading
// stopped on failure; callers treat any failure as fatal for the parse.
inline bool read_sleb33(const std::uint8_t *data, std::size_t size, std::size_t *pos,
                        std::int64_t *out)
{
  std::uint64_t value = 0;
  unsigned shift = 0;
  for ( unsigned i = 0; i < 5; ++i )
  {
    if ( *pos >= size )
      return false;
    const std::uint8_t b = data[(*pos)++];
    const unsigned remaining = 33 - shift;
    const std::uint8_t payload = b & 0x7F;
    if ( remaining < 7 )
    {
      const std::uint8_t low_mask = std::uint8_t((1u << remaining) - 1);
      const std::uint8_t high_mask = std::uint8_t(0x7F ^ low_mask);
      const bool negative = (payload & (1u << (remaining - 1))) != 0;
      if ( negative ? (payload & high_mask) != high_mask : (payload & high_mask) != 0 )
        return false;
    }
    value |= std::uint64_t(payload) << shift;
    shift += 7;
    if ( (b & 0x80) == 0 )
    {
      if ( out != nullptr )
      {
        if ( (b & 0x40) != 0 && shift < 64 )
          value |= (~std::uint64_t(0)) << shift;
        *out = std::int64_t(value);
      }
      return true;
    }
  }
  return false;
}

} // namespace wasmhex::loader
