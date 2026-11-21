// The valid_utf8 definition for the binary/reader.hpp declaration
// (module_t::parse enforced UTF-8 names before the reader was split
// out). Moved out of module.cpp so wasmhex_binary is self-contained.
#include "reader.hpp"

namespace wasmhex::loader
{
bool valid_utf8(const std::uint8_t *data, size_t size)
{
  for ( size_t i = 0; i < size; )
  {
    const uint8_t first = data[i++];
    if ( first <= 0x7F )
      continue;
    const unsigned length = first >= 0xC2 && first <= 0xDF ? 2
      : first >= 0xE0 && first <= 0xEF                     ? 3
      : first >= 0xF0 && first <= 0xF4                     ? 4
                                                           : 0;
    if ( length == 0 || length - 1 > size - i )
      return false;
    uint32_t codepoint = first & (length == 2 ? 0x1F : length == 3 ? 0x0F : 0x07);
    for ( unsigned j = 1; j < length; ++j )
    {
      const uint8_t continuation = data[i++];
      if ( (continuation & 0xC0) != 0x80 )
        return false;
      codepoint = (codepoint << 6) | (continuation & 0x3F);
    }
    if ( (length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800)
         || (length == 4 && codepoint < 0x10000) || (codepoint >= 0xD800 && codepoint <= 0xDFFF)
         || codepoint > 0x10FFFF )
      return false;
  }
  return true;
}
} // namespace wasmhex::loader
