// Emscripten EH evidence — implementation. See eh.hpp.
#include "eh.hpp"

#include "../../binary/instruction_stream.hpp"
#include "../../types.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

namespace wasmhex::loader
{

namespace
{
constexpr std::uint32_t kEhIrMagic = 0x45484952u; // "EHIR"
constexpr std::uint32_t kEhIrVersion = 1u;

// (Verbatim name semantics of the old ctree layer's helpers.)
bool strip_leading_underscores(const char *&name)
{
  while ( *name == '_' )
    ++name;
  return true;
}
} // namespace

// invoke_[vijfd]+ helper names (the same strict charset the ctree
// layer's render path checks).
bool is_invoke_helper_name(const char *name)
{
  if ( name == nullptr || std::strncmp(name, "invoke_", 7) != 0 )
    return false;
  const char *rest = name + 7;
  if ( *rest == '\0' )
    return false;
  for ( const char *p = rest; *p != '\0'; ++p )
    if ( *p != 'v' && *p != 'i' && *p != 'j' && *p != 'f' && *p != 'd' )
      return false;
  return true;
}

bool is_find_matching_catch_name(const char *name)
{
  if ( name == nullptr )
    return false;
  strip_leading_underscores(name);
  return std::strncmp(name, "cxa_find_matching_catch", 23) == 0;
}

bool is_find_matching_catch_2_name(const char *name)
{
  if ( name == nullptr )
    return false;
  strip_leading_underscores(name);
  return std::strcmp(name, "cxa_find_matching_catch_2") == 0;
}

bool is_resume_exception_name(const char *name)
{
  if ( name == nullptr )
    return false;
  strip_leading_underscores(name);
  return std::strcmp(name, "resumeException") == 0;
}

bool is_builtin_trap_name(const char *name)
{
  if ( name == nullptr )
    return false;
  return std::strcmp(name, "__builtin_trap") == 0 || std::strcmp(name, "_builtin_trap") == 0;
}

bool eh_invoke_site_ir_t::invoke_site_valid(std::uint32_t body_size) const
{
  if ( call_offset == UINT32_MAX || call_offset >= body_size )
    return false;
  // Identity-only mark: the invoke identity comes from the import
  // table; no protocol evidence is claimed.
  if ( threw_global == UINT32_MAX && tee_local == UINT32_MAX && zero_store_before == UINT32_MAX
       && read_offset == UINT32_MAX )
  {
    return true;
  }
  if ( threw_global == UINT32_MAX || tee_local == UINT32_MAX )
    return false;
  if ( zero_store_before == UINT32_MAX )
    return false;
  if ( zero_store_before >= call_offset )
    return false;
  // The noreturn-throw path has no flag read after the call; when present it
  // must sit after the call.
  if ( read_offset == UINT32_MAX )
    return true;
  if ( read_offset <= call_offset )
    return false;
  // The reset store may be absent on reduced shapes; when present it must
  // sit between the read and (optionally) the guard branch.
  if ( zero_store_after != UINT32_MAX
       && (zero_store_after <= read_offset
           || (guard_br_offset != UINT32_MAX && zero_store_after >= guard_br_offset)) )
    return false;
  return true;
}

bool eh_pad_ir_t::pad_valid(std::uint32_t body_size) const
{
  if ( pad_offset == UINT32_MAX || pad_offset >= body_size )
    return false;
  if ( catch_offset == UINT32_MAX || catch_offset < pad_offset )
    return false;
  if ( resume_offset == UINT32_MAX || resume_offset <= catch_offset )
    return false;
  if ( trap_offset == UINT32_MAX || trap_offset <= resume_offset )
    return false;
  return true;
}

void encode_eh_function_ir_blob(const eh_function_ir_t &ir, std::vector<std::uint32_t> *out)
{
  if ( out == nullptr )
    return;
  out->clear();
  if ( !ir.present )
    return;
  out->push_back(kEhIrMagic);
  out->push_back(kEhIrVersion);
  out->push_back(std::uint32_t(ir.invokes.size()));
  for ( const eh_invoke_site_ir_t &s : ir.invokes )
  {
    out->push_back(s.call_offset);
    out->push_back(s.threw_global);
    out->push_back(s.tee_local);
    out->push_back(s.tee_offset);
    out->push_back(s.zero_store_before);
    out->push_back(s.read_offset);
    out->push_back(s.zero_store_after);
    out->push_back(s.guard_br_offset);
    out->push_back(s.pad_offset);
    out->push_back(s.wrapped_invoke_fn);
  }
  out->push_back(std::uint32_t(ir.pads.size()));
  for ( const eh_pad_ir_t &p : ir.pads )
  {
    out->push_back(p.pad_offset);
    out->push_back(p.catch_offset);
    out->push_back(p.catch_fn);
    out->push_back(p.resume_offset);
    out->push_back(p.trap_offset);
    out->push_back(p.cleanup_count);
    out->push_back((p.is_catch_all ? 1u : 0u));
  }
}

bool decode_eh_function_ir_blob(const std::uint32_t *words, std::size_t word_count,
                                eh_function_ir_t *out)
{
  if ( out == nullptr )
    return false;
  *out = eh_function_ir_t{};
  if ( words == nullptr || word_count < 3 || words[0] != kEhIrMagic || words[1] != kEhIrVersion )
    return false;
  std::size_t cursor = 2;
  const std::size_t site_count = std::size_t(words[cursor++]);
  if ( site_count > kEhMaxInvokeSites || site_count > (word_count - cursor) / 10 )
    return false;
  out->invokes.resize(site_count);
  for ( std::size_t i = 0; i < site_count; ++i )
  {
    eh_invoke_site_ir_t &s = out->invokes[i];
    s.call_offset = words[cursor++];
    s.threw_global = words[cursor++];
    s.tee_local = words[cursor++];
    s.tee_offset = words[cursor++];
    s.zero_store_before = words[cursor++];
    s.read_offset = words[cursor++];
    s.zero_store_after = words[cursor++];
    s.guard_br_offset = words[cursor++];
    s.pad_offset = words[cursor++];
    s.wrapped_invoke_fn = words[cursor++];
  }
  if ( cursor >= word_count )
    return false;
  const std::size_t pad_count = std::size_t(words[cursor++]);
  if ( pad_count > kEhMaxPads || pad_count > (word_count - cursor) / 7 )
    return false;
  out->pads.resize(pad_count);
  for ( std::size_t i = 0; i < pad_count; ++i )
  {
    eh_pad_ir_t &p = out->pads[i];
    p.pad_offset = words[cursor++];
    p.catch_offset = words[cursor++];
    p.catch_fn = words[cursor++];
    p.resume_offset = words[cursor++];
    p.trap_offset = words[cursor++];
    p.cleanup_count = words[cursor++];
    const std::uint32_t flags = words[cursor++];
    if ( flags > 1u )
      return false;
    p.is_catch_all = flags != 0;
  }
  out->present = site_count != 0 || pad_count != 0;
  return cursor == word_count;
}

// legalize-i64 wrapper body (moved verbatim from rewriter_eh.cpp)
// Fail-closed detector for Emscripten legalize-i64 wrapper bodies around
// invoke_* imports: canonical arg-passing shapes where each parameter
// is either passed through (`local.get p`) or i64-split (`local.get p;
// i32.wrap_i64; local.get p; i64.const 32; i64.shr_u; i32.wrap_i64`),
// followed by ONE `call <invoke_idx>` and an optional i64-return combine
// (`i64.extend_i32_u; global.get G; i64.extend_i32_u; i64.const 32;
// i64.shl; i64.or`). At least one split or combine must be present, and the
// body must end with `end` at the exact boundary.

bool match_legalize_i64_wrapper_body(const std::uint8_t *data, std::size_t size,
                                     std::uint32_t *out_callee_func_idx)
{
  if ( data == nullptr || size == 0 || out_callee_func_idx == nullptr )
    return false;
  binary::insn_cursor_t cursor{ data, size, 0, nullptr };
  binary::decoded_insn_t insns[128];
  std::size_t count = 0;
  while ( cursor.pos < cursor.size )
  {
    if ( count >= sizeof(insns) / sizeof(insns[0]) )
      return false;
    if ( !binary::next_insn(cursor, &insns[count]) )
      return false;
    ++count;
  }
  if ( cursor.pos != cursor.size || count < 3 )
    return false;
  if ( insns[count - 1].opcode != 0x0B )
    return false;
  for ( std::size_t k = 0; k + 1 < count; ++k )
    if ( insns[k].opcode == 0x0B )
      return false;

  // Parameter 0 is the function pointer / table index forwarded to invoke_*.
  if ( insns[0].opcode != 0x20 || insns[0].imm[0] != 0 )
    return false;

  std::size_t i = 1;
  std::uint64_t expected_local = 1;
  bool has_i64_split = false;

  while ( i < count && insns[i].opcode == 0x20 )
  {
    const std::uint64_t loc = insns[i].imm[0];
    if ( loc != expected_local )
      return false;
    if ( i + 5 < count && insns[i + 1].opcode == 0xA7 )
    {
      if ( insns[i + 2].opcode != 0x20 || insns[i + 2].imm[0] != loc || insns[i + 3].opcode != 0x42
           || insns[i + 3].imm[0] != 32 || insns[i + 4].opcode != 0x88
           || insns[i + 5].opcode != 0xA7 )
      {
        return false;
      }
      has_i64_split = true;
      i += 6;
    }
    else
    {
      i += 1;
    }
    ++expected_local;
  }

  if ( i >= count || insns[i].opcode != 0x10 || insns[i].imm[0] > UINT32_MAX )
    return false;
  const std::uint32_t callee_func_idx = std::uint32_t(insns[i].imm[0]);
  ++i;

  bool has_i64_ret = false;
  if ( i + 6 == count - 1 )
  {
    if ( insns[i].opcode != 0xAD || insns[i + 1].opcode != 0x23 || insns[i + 2].opcode != 0xAD
         || insns[i + 3].opcode != 0x42 || insns[i + 3].imm[0] != 32 || insns[i + 4].opcode != 0x86
         || insns[i + 5].opcode != 0x84 )
    {
      return false;
    }
    has_i64_ret = true;
    i += 6;
  }

  if ( i != count - 1 || (!has_i64_split && !has_i64_ret) )
    return false;

  *out_callee_func_idx = callee_func_idx;
  return true;
}

} // namespace wasmhex::loader
