// Libc++ SSO dtor evidence — codec + name helpers. See libcxx.hpp.
#include "libcxx.hpp"

#include <cstring>

namespace wasmhex::loader
{

namespace
{
constexpr std::uint32_t kLibcxxIrMagic = 0x4C435849u; // "LCXI"
constexpr std::uint32_t kLibcxxIrVersion = 1u;
} // namespace

bool is_free_callee_name(const char *name)
{
  if ( name == nullptr )
    return false;
  while ( *name == '_' )
    ++name;
  return std::strcmp(name, "free") == 0;
}

bool libcxx_sso_dtor_ir_t::valid(std::uint32_t body_size) const
{
  if ( guard_offset == UINT32_MAX || guard_offset >= body_size )
    return false;
  if ( tag_load_offset == UINT32_MAX || tag_load_offset >= guard_offset )
    return false;
  if ( free_call_offset == UINT32_MAX || free_call_offset <= guard_offset
       || free_call_offset >= body_size )
    return false;
  if ( free_fn == UINT32_MAX )
    return false;
  return form >= std::uint8_t(libcxx_sso_form_t::pointer_tag8)
    && form <= std::uint8_t(libcxx_sso_form_t::stack_tag32);
}

void encode_libcxx_function_ir_blob(const libcxx_function_ir_t &ir, std::vector<std::uint32_t> *out)
{
  if ( out == nullptr )
    return;
  out->clear();
  if ( !ir.present )
    return;
  out->push_back(kLibcxxIrMagic);
  out->push_back(kLibcxxIrVersion);
  out->push_back(std::uint32_t(ir.sso_dtors.size()));
  for ( const libcxx_sso_dtor_ir_t &s : ir.sso_dtors )
  {
    out->push_back(s.guard_offset);
    out->push_back(s.tag_load_offset);
    out->push_back(s.free_call_offset);
    out->push_back(s.free_fn);
    out->push_back(s.form);
  }
}

bool decode_libcxx_function_ir_blob(const std::uint32_t *words, std::size_t word_count,
                                    libcxx_function_ir_t *out)
{
  if ( out == nullptr )
    return false;
  *out = libcxx_function_ir_t{};
  if ( words == nullptr || word_count < 3 || words[0] != kLibcxxIrMagic
       || words[1] != kLibcxxIrVersion )
    return false;
  std::size_t cursor = 2;
  const std::size_t count = std::size_t(words[cursor++]);
  if ( count > kLibcxxMaxSsoDtors || count > (word_count - cursor) / 5 )
    return false;
  out->sso_dtors.resize(count);
  for ( std::size_t i = 0; i < count; ++i )
  {
    libcxx_sso_dtor_ir_t &s = out->sso_dtors[i];
    s.guard_offset = words[cursor++];
    s.tag_load_offset = words[cursor++];
    s.free_call_offset = words[cursor++];
    s.free_fn = words[cursor++];
    s.form = std::uint8_t(words[cursor++]);
    if ( s.form == 0 || s.form > std::uint8_t(libcxx_sso_form_t::stack_tag32) )
      return false;
  }
  out->present = count != 0;
  return cursor == word_count;
}

} // namespace wasmhex::loader
