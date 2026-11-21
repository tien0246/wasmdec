#include "types.hpp"
#include "validation/function_validator.hpp"
#include "binary/instructions.hpp"
#include "binary/instruction_stream.hpp"

#include <algorithm>
#include <cstring>

namespace wasmhex::loader
{

void module_t::scan_go_provenance()
{
  go_function_shapes.clear();
  go_provenance = false;
  if ( !is_go || defined_func_types.empty() )
    return;
  bool globals_ok = globals.size() >= 8;
  if ( globals_ok )
  {
    static const uchar want[8] = { 0x7F, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7F };
    for ( size_t i = 0; i < 8; ++i )
      globals_ok = globals[i].type == want[i] && globals[i].mutability != 0;
  }
  if ( !globals_ok )
    return;
  size_t pc_b_count = 0;
  size_t prologue_count = 0;
  go_function_shapes.resize(defined_func_types.size(), 0);
  for ( size_t i = 0; i < defined_func_types.size(); ++i )
  {
    if ( defined_func_types[i] >= types.size() || i >= bodies.size() )
      continue;
    const wasm_type_t &type = types[defined_func_types[i]];
    if ( type.params.size() != 1 || type.params[0] != 0x7F || type.results.size() != 1
         || type.results[0] != 0x7F )
    {
      continue;
    }
    uchar shape = 1; // PC_B signature
    ++pc_b_count;
    const body_t &body = bodies[i];
    if ( body.offset < code.size() )
    {
      const uchar *p = code.data() + body.offset;
      // A Go signature on one function must not be inferred from bytes
      // belonging to the next function in the flattened code stream.
      const size_t avail = std::min(size_t(body.size), code.size() - size_t(body.offset));
      // Walk the decoded instruction stream instead of scanning raw
      // bytes.
      binary::insn_cursor_t cursor{ p, avail, 0, nullptr };
      std::vector<binary::decoded_insn_t> head;
      head.reserve(256);
      binary::decoded_insn_t decoded;
      while ( cursor.pos < 512 && binary::next_insn(cursor, &decoded) )
        head.push_back(decoded);

      if ( head.size() >= 2 && head[0].opcode == 0x23 && head[0].imm[0] == 0 )
      {
        if ( head[1].opcode == 0x21 )
        {
          shape |= 2; // SP copy prologue
          ++prologue_count;
        }
        else if ( head.size() >= 4 && head[1].opcode == 0x41 && head[2].opcode == 0x6B
                  && head[3].opcode == 0x24 && head[3].imm[0] == 0 )
        {
          shape |= 2; // frame-sub prologue
          ++prologue_count;
        }
      }
      // go1.26 prologues chain block/loop headers before the dispatcher
      // (`global.get SP; local.set 1; block; ...; block; local.get 0;
      // br_table`) — large entry ladders put the br_table hundreds of
      // bytes in, so the dispatch window spans the body head generously.
      for ( size_t k = 0; k + 1 < head.size(); ++k )
      {
        if ( head[k].opcode == 0x20 && head[k].imm[0] == 0 && head[k + 1].opcode == 0x0E
             && head[k + 1].offset < 512 )
        {
          shape |= 4; // entryPointLoop dispatch at the head
          break;
        }
      }
    }
    go_function_shapes[i] = shape;
  }
  const size_t defined = defined_func_types.size();
  if ( pc_b_count * 2 <= defined )
    return;
  if ( prologue_count * 2 <= pc_b_count )
    return;
  go_provenance = true;
}

void module_t::scan_go_calls()
{
  // Go call scanning and Go function IR construction run once in
  // build_go_function_irs() at the end of module_t::parse() after the Go
  // profile is resolved.
}
void module_t::resolve_go_profile()
{
  go_profile.mismatch_reason.clear();
  if ( !is_go )
  {
    go_profile.profile = go_profile_t::none;
    return;
  }

  // Layer 2: producers version -> the specific family (when parseable).
  go_profile_t producers_family = go_profile_t::none;
  if ( !go_profile.producers_version.empty() )
  {
    int major = 0, minor = 0;
    if ( parse_go_version_string(go_profile.producers_version, &major, &minor) )
      producers_family = go_profile_for_version(major, minor);
  }

  // Layer 1: import namespace era check ("go" means <=1.20).
  const bool namespace_legacy = go_profile.import_namespace == "go";
  const bool namespace_modern = go_profile.import_namespace == "gojs";

  // Layer 3 (informational only): every Go-wasm corpus we hold — 1.19.3,
  // 1.26.5, 1.27.3 — carries the 0xFFFFFFF0 wire, so the magic does NOT
  // split the eras on this target (the assumption came from
  // native Go). Kept as recorded evidence; it may not contradict.

  auto contradicts = [](go_profile_t family, bool legacy_namespace, bool legacy_wire)
  {
    const bool family_legacy = family == go_profile_t::go1_19_legacy;
    if ( legacy_namespace && !family_legacy )
      return true;
    if ( legacy_wire && !family_legacy )
      return true;
    return false;
  };

  if ( producers_family != go_profile_t::none && producers_family != go_profile_t::unknown )
  {
    if ( contradicts(producers_family, namespace_legacy, false) )
    {
      go_profile.profile = go_profile_t::unknown;
      go_profile.mismatch_reason = "go profile mismatch: producers says "
        + go_profile.producers_version + " but the import namespace ("
        + (go_profile.import_namespace.empty() ? "?" : go_profile.import_namespace)
        + ") and/or pclntab wire ("
        + (go_profile.pclntab_wire.empty() ? "?" : go_profile.pclntab_wire)
        + ") belong to a different era";
      return;
    }
    go_profile.profile = producers_family;
  }
  else if ( namespace_legacy )
    go_profile.profile = go_profile_t::go1_19_legacy;
  else if ( namespace_modern )
    go_profile.profile = go_profile_t::go1_23_series; // no version string: era only
  else
  {
    go_profile.profile = go_profile_t::unknown;
    go_profile.mismatch_reason
      = "go profile mismatch: Go imports present but no "
        "version evidence resolved (producers/version/namespace/wire all absent)";
    return;
  }

  // verify(): module reality may RAISE the era, never lower it. The
  // V-registers are emitted on demand (a module without SIMD has no v128
  // types even on 1.26+), so absence proves nothing — but PRESENCE of a
  // v128 type needs a profile from the V-register era (>=1.25).
  go_profile.apply_expectations();
  if ( go_profile.resolved_go() && !go_profile.v_registers_expects() )
  {
    bool has_v128 = false;
    for ( const wasm_type_t &type : types )
      for ( uchar param : type.params )
        if ( param == 0x7B )
          has_v128 = true;
    if ( has_v128 )
    {
      go_profile.profile = go_profile_t::unknown;
      go_profile.mismatch_reason = "go profile mismatch: module uses v128 "
                                   "types but the profile predates the V-register codegen";
    }
  }
}

} // namespace wasmhex::loader
