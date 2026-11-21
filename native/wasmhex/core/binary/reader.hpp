// Byte reader over a flat buffer (split out of core/types.hpp;
// types.hpp re-includes this header as a shim so existing include sites
// keep compiling). Self-contained: plain std:: integer types only.
#pragma once

#include <cstdint>
#include <string>

#include "leb128.hpp"

namespace wasmhex::loader
{

bool valid_utf8(const std::uint8_t *data, std::size_t size);

struct reader_t
{
  const std::uint8_t *data;
  std::size_t size;
  std::size_t pos = 0;

  bool byte(std::uint8_t *v)
  {
    if ( pos == size )
      return false;
    *v = data[pos++];
    return true;
  }

  bool uleb(std::uint64_t *v, unsigned bits = 32)
  {
    std::uint64_t value = 0;
    unsigned shift = 0;
    const unsigned max_bytes = (bits + 6) / 7;
    for ( unsigned i = 0; i < max_bytes; ++i )
    {
      std::uint8_t b;
      if ( !byte(&b) )
        return false;
      const std::uint64_t part = b & 0x7F;
      if ( shift >= bits || part > (UINT64_MAX >> shift) )
        return false;
      value |= part << shift;
      if ( (b & 0x80) == 0 )
      {
        if ( bits < 64 && value >= (std::uint64_t(1) << bits) )
          return false;
        *v = value;
        return true;
      }
      shift += 7;
    }
    return false;
  }

  bool sleb33(std::int64_t *out = nullptr) { return read_sleb33(data, size, &pos, out); }

  bool sleb64(std::int64_t *out)
  {
    std::uint64_t value = 0;
    unsigned shift = 0;
    for ( unsigned i = 0; i < 10; ++i )
    {
      std::uint8_t b;
      if ( !byte(&b) )
        return false;
      std::uint8_t payload = b & 0x7F;
      const unsigned remaining = 64 - shift;
      if ( remaining < 7 )
      {
        const std::uint8_t low_mask = std::uint8_t((1u << remaining) - 1);
        const std::uint8_t high_mask = std::uint8_t(0x7F ^ low_mask);
        const bool negative = (payload & (1u << (remaining - 1))) != 0;
        if ( negative ? (payload & high_mask) != high_mask : (payload & high_mask) != 0 )
          return false;
        payload &= low_mask;
      }
      value |= std::uint64_t(payload) << shift;
      shift += 7;
      if ( (b & 0x80) == 0 )
      {
        if ( (b & 0x40) != 0 && shift < 64 )
          value |= (~std::uint64_t(0)) << shift;
        *out = std::int64_t(value);
        return true;
      }
    }
    return false;
  }

  bool fixed(std::uint64_t *out, std::size_t bytes)
  {
    if ( bytes == 0 || bytes > 8 || bytes > size - pos )
      return false;
    std::uint64_t value = 0;
    for ( std::size_t i = 0; i < bytes; ++i )
      value |= std::uint64_t(data[pos++]) << (i * 8);
    *out = value;
    return true;
  }

  bool name(std::string *out)
  {
    std::uint64_t len;
    if ( !uleb(&len) || len > size - pos || !valid_utf8(data + pos, std::size_t(len)) )
      return false;
    out->assign(reinterpret_cast<const char *>(data + pos), std::size_t(len));
    pos += std::size_t(len);
    return true;
  }

  bool skip(std::size_t len)
  {
    if ( len > size - pos )
      return false;
    pos += len;
    return true;
  }

  bool skip_valtype()
  {
    std::uint8_t type;
    if ( !byte(&type) )
      return false;
    return type != 0x63 && type != 0x64 ? true : sleb33();
  }

  bool vector_count(std::uint64_t *count) { return uleb(count) && *count <= size - pos; }
};

} // namespace wasmhex::loader
