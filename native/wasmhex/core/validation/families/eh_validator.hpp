// Exception handling + typed function-reference validation helpers
// header-only extension for function_validator.cpp.
// Covers the exception-handling proposal (try_table/throw/throw_ref plus
// the legacy try/catch/catch_all/delegate/rethrow encoding) and the typed
// function-reference instructions (call_ref, return_call_ref,
// ref.as_non_null, br_on_null, br_on_non_null, ref.eq). The signatures are
// expressed against the typed operand stack; the caller applies them and
// consults the module for tag/type indices.
#pragma once

#include "../../types.hpp"

#include <cstdint>
#include <vector>

namespace wasmhex::loader
{

constexpr uchar eh_vt_i32 = 0x7F;
constexpr uchar eh_vt_exnref = 0x69; // spec: exnref abstract handle
constexpr uchar eh_vt_funcref = 0x70;

// try_table (0x1F): block type + catch clause vector.
struct eh_catch_clause_t
{
  enum kind_t : uchar
  {
    catch_tag = 0,     // tag index
    catch_tag_ref = 1, // tag index + exnref on the handler stack
    catch_all = 2,
    catch_all_ref = 3,
  } kind = catch_all;
  uint32 tag = UINT32_MAX;   // tag index for the tag forms
  uint32 depth = UINT32_MAX; // label depth of the handler block
};

struct eh_try_table_t
{
  bool ok = false;
  const char *reason = nullptr;
  uint64 block_type = 0; // raw blocktype byte / sleb33 type index
  std::vector<eh_catch_clause_t> clauses;
};

// Parse a try_table immediate (after the 0x1F opcode byte): blocktype
// (0x40 void, a value type byte, or a positive sleb33 type index), clause
// count, then the clauses. Only the encoding shape is checked here; tag
// existence/arity is resolved by the caller through the module tables.
inline eh_try_table_t eh_parse_try_table(reader_t &r)
{
  eh_try_table_t out;
  int64 block_type = 0;
  if ( !r.sleb33(&block_type) )
  {
    out.reason = "try_table block type is truncated";
    return out;
  }
  out.block_type = uint64(block_type);
  uint64 count = 0;
  if ( !r.uleb(&count) || count > 0x10000 )
  {
    out.reason = "try_table clause vector is malformed";
    return out;
  }
  out.clauses.reserve(size_t(count));
  for ( uint64 i = 0; i < count; ++i )
  {
    uchar kind = 0;
    if ( !r.byte(&kind) || kind > 3 )
    {
      out.reason = "try_table catch clause kind is malformed";
      return out;
    }
    eh_catch_clause_t clause;
    clause.kind = eh_catch_clause_t::kind_t(kind);
    if ( clause.kind == eh_catch_clause_t::catch_tag
         || clause.kind == eh_catch_clause_t::catch_tag_ref )
    {
      uint64 tag = 0;
      if ( !r.uleb(&tag) || tag > UINT32_MAX )
      {
        out.reason = "try_table catch tag index is malformed";
        return out;
      }
      clause.tag = uint32(tag);
    }
    uint64 depth = 0;
    if ( !r.uleb(&depth) || depth > UINT32_MAX )
    {
      out.reason = "try_table catch label is malformed";
      return out;
    }
    clause.depth = uint32(depth);
    out.clauses.push_back(clause);
  }
  out.ok = true;
  return out;
}

// Instruction signatures. `params`/`results` exclude the shared parts the
// caller already models (block results for branches, function types for
// call_ref) - they cover only what EH adds on top.
enum eh_insn_t : uchar
{
  EH_LEGACY_TRY = 0x06,     // blocktype
  EH_LEGACY_CATCH = 0x07,   // tag
  EH_LEGACY_THROW = 0x08,   // tag
  EH_LEGACY_RETHROW = 0x09, // depth
  EH_THROW_REF = 0x0A,
  EH_LEGACY_DELEGATE = 0x18, // depth
  EH_LEGACY_CATCH_ALL = 0x19,
  EH_CALL_REF = 0x14,        // type index
  EH_RETURN_CALL_REF = 0x15, // type index
  EH_REF_EQ = 0xD3,
  EH_REF_AS_NON_NULL = 0xD4,
  EH_BR_ON_NULL = 0xD5,     // depth
  EH_BR_ON_NON_NULL = 0xD6, // depth
  EH_TRY_TABLE = 0x1F,
};

struct eh_sig_t
{
  bool known = false;
  bool pops_exnref = false;   // throw_ref / br_on_null variants
  bool pushes_exnref = false; // catch_ref handlers (documentation)
  uchar operand = 0;          // reference operand consumed
  uchar result = 0;           // produced scalar/reference
  bool result_i32 = false;    // ref.eq
};

inline eh_sig_t eh_signature(uchar opcode)
{
  switch ( opcode )
  {
    case EH_THROW_REF: return eh_sig_t{ true, true, false, eh_vt_exnref, 0, false };
    case EH_REF_EQ: return eh_sig_t{ true, false, false, 0, eh_vt_i32, true };
    case EH_REF_AS_NON_NULL:
      return eh_sig_t{ true, false, false, 0, 0, false }; // (ref) -> (ref), caller approximates
    case EH_BR_ON_NULL:
    case EH_BR_ON_NON_NULL:
      return eh_sig_t{ true, false, false, 0, 0, false }; // label glue at the caller
    case EH_CALL_REF:
    case EH_RETURN_CALL_REF:
      return eh_sig_t{ true, false, false, 0, 0, false }; // type glue at the caller
    default: return eh_sig_t{};
  }
}

// Tag arity helper for throw/catch: the caller resolves the tag's type
// index through module.tag_type_indices and passes the params here.
inline bool eh_tag_params_match(const std::vector<uchar> &tag_params,
                                const std::vector<uchar> &stack_top)
{
  if ( tag_params.size() != stack_top.size() )
    return false;
  for ( size_t i = 0; i < tag_params.size(); ++i )
    if ( tag_params[i] != stack_top[i] )
      return false;
  return true;
}

} // namespace wasmhex::loader
