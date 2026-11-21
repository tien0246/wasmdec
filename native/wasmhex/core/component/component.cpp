// WASM component-model container handling. A component
// wraps zero or more embedded core modules; wasmhex decompiles module 0 and
// lists the rest (entry.cpp) — the layer-order walk lives here, SDK-free.
#include "component.hpp"

#include <cstring> // memcmp: declare directly, never via transitive includes

namespace wasmhex::loader
{
bool find_embedded_core_modules(const uchar *data, size_t size, size_t base_offset, unsigned depth,
                                std::vector<embedded_core_module_t> *modules, std::string *error)
{
  static const uchar component_header[] = {
    0x00, 'a', 's', 'm', 0x0D, 0x00, 0x01, 0x00,
  };
  if ( depth >= 64 )
  {
    *error = format("WASM component nesting limit exceeded at file offset 0x%llX",
                    static_cast<unsigned long long>(base_offset));
    return false;
  }
  if ( size < sizeof(component_header)
       || memcmp(data, component_header, sizeof(component_header)) != 0 )
  {
    *error = format("Unsupported or malformed WASM component header at file offset 0x%llX",
                    static_cast<unsigned long long>(base_offset));
    return false;
  }

  reader_t reader = { data, size, sizeof(component_header) };
  size_t section_count = 0;
  uchar last_supported_section = 0;
  while ( reader.pos < reader.size )
  {
    const size_t section_offset = base_offset + reader.pos;
    uchar id;
    uint64 payload_size;
    if ( ++section_count > 65'536 || !reader.byte(&id) || !reader.uleb(&payload_size)
         || payload_size > reader.size - reader.pos )
    {
      *error = format("Malformed WASM component section frame at file offset 0x%llX",
                      static_cast<unsigned long long>(section_offset));
      return false;
    }
    const uchar *payload = reader.data + reader.pos;
    const size_t payload_offset = base_offset + reader.pos;
    reader.pos += size_t(payload_size);
    if ( id == 0 )
    {
      reader_t custom = { payload, size_t(payload_size) };
      std::string name;
      if ( !custom.name(&name) )
      {
        *error = format("Malformed WASM component custom section name at file offset 0x%llX",
                        static_cast<unsigned long long>(payload_offset));
        return false;
      }
    }
    else if ( id == 1 || id == 4 )
    {
      if ( id < last_supported_section )
      {
        *error = format("Out-of-order WASM component section %u at file offset 0x%llX; "
                        "supported sections must place section 1 before section 4",
                        unsigned(id), static_cast<unsigned long long>(section_offset));
        return false;
      }
      last_supported_section = id;
      // Collect every embedded core module (component layer order rule
      // only requires section 1 before section 4, so repeated section-1
      // entries are legal). The loader decompiles module 0 and records the
      // rest instead of silently dropping or rejecting the component.
      if ( id == 1 )
      {
        modules->push_back({ payload, size_t(payload_size), payload_offset });
      }
      else if ( !find_embedded_core_modules(payload, size_t(payload_size), payload_offset,
                                            depth + 1, modules, error) )
      {
        return false;
      }
    }
    else
    {
      *error = format("Unsupported WASM component section %u at file offset 0x%llX; "
                      "wasmhex F5 validates only custom, embedded core-module, and "
                      "nested-component sections",
                      unsigned(id), static_cast<unsigned long long>(section_offset));
      return false;
    }
  }
  return true;
}
} // namespace wasmhex::loader
