// Emscripten EH evidence: the loader-side builder (core sources; the
// processor only consumes blob 'i' via the helpers in eh.cpp).
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

struct zero_store_hit_t
{
  std::uint32_t tee_offset = UINT32_MAX;
  std::uint32_t store_offset = UINT32_MAX;
  std::uint32_t threw_global = UINT32_MAX;
  std::uint32_t tee_local = UINT32_MAX;
};

bool find_protocol_prologue(const std::vector<binary::decoded_insn_t> &insns, std::size_t call_idx,
                            const std::set<std::uint32_t> &threw_globals, zero_store_hit_t *out)
{
  std::size_t i = call_idx;
  int steps = 0;
  while ( i >= 1 && steps < 80 )
  {
    // ... ; global.get G ; local.tee/local.set T ; i32.const 0 ;
    // i32.store [T+0] (the store is the LAST insn before the argument
    // pushes; cxa_throw wrappers spell the pointer cache with local.set)
    if ( insns[i - 1].opcode == 0x36 && insns[i - 1].imm[0] == 2 && insns[i - 1].imm[1] == 0
         && i >= 4 && insns[i - 2].opcode == 0x41 && insns[i - 2].imm[0] == 0
         && (insns[i - 3].opcode == 0x22 || insns[i - 3].opcode == 0x21)
         && insns[i - 4].opcode == 0x23 )
    {
      const std::uint32_t g = std::uint32_t(insns[i - 4].imm[0]);
      const std::uint32_t t = std::uint32_t(insns[i - 3].imm[0]);
      if ( threw_globals.count(g) != 0 )
      {
        // Between the store and the call, nothing may read/write [T] or G.
        bool clean = true;
        for ( std::size_t q = i - 1; q < call_idx; ++q )
        {
          if ( insns[q].opcode == 0x23 && std::uint32_t(insns[q].imm[0]) == g )
            clean = false;
          if ( (insns[q].opcode == 0x28 || insns[q].opcode == 0x36)
               && std::uint32_t(insns[q].imm[1]) == 0 && q >= 1 && insns[q - 1].opcode == 0x20
               && std::uint32_t(insns[q - 1].imm[0]) == t )
            clean = false;
        }
        if ( clean )
        {
          out->tee_offset = insns[i - 4].offset;
          out->store_offset = insns[i - 1].offset;
          out->threw_global = g;
          out->tee_local = t;
          return true;
        }
      }
    }
    // fragmented zero store: local.get T ; i32.const 0 ; i32.store [T+0]
    if ( insns[i - 1].opcode == 0x36 && insns[i - 1].imm[0] == 2 && insns[i - 1].imm[1] == 0
         && i >= 3 && insns[i - 2].opcode == 0x41 && insns[i - 2].imm[0] == 0
         && insns[i - 3].opcode == 0x20 )
    {
      const std::uint32_t t = std::uint32_t(insns[i - 3].imm[0]);
      bool clean = true;
      for ( std::size_t q = i - 1; q < call_idx; ++q )
      {
        if ( insns[q].opcode == 0x23 && threw_globals.count(std::uint32_t(insns[q].imm[0])) != 0 )
          clean = false;
        if ( (insns[q].opcode == 0x28 || insns[q].opcode == 0x36)
             && std::uint32_t(insns[q].imm[1]) == 0 && q >= 1 && insns[q - 1].opcode == 0x20
             && std::uint32_t(insns[q - 1].imm[0]) == t )
          clean = false;
      }
      if ( clean )
      {
        // the pointer cache: scan back for global.get G ; set|tee T; a nearer
        // plain redefinition of T means the cached value may be stale
        for ( std::size_t j = i - 2; j >= 2; --j )
        {
          if ( (insns[j - 1].opcode == 0x21 || insns[j - 1].opcode == 0x22)
               && std::uint32_t(insns[j - 1].imm[0]) == t )
          {
            if ( insns[j - 2].opcode == 0x23
                 && threw_globals.count(std::uint32_t(insns[j - 2].imm[0])) != 0 )
            {
              out->tee_offset = insns[j - 2].offset;
              out->store_offset = insns[i - 1].offset;
              out->threw_global = std::uint32_t(insns[j - 2].imm[0]);
              out->tee_local = t;
              return true;
            }
            break; // stale: T redefined from something else
          }
        }
      }
    }
    --i;
    ++steps;
  }
  return false;
}

// Forward from the call: `[local.set R]? ; local.get T ; i32.load [T+0] ;
// local.set V ; local.get T ; i32.const 0 ; i32.store [T+0] ; guard`.
bool find_protocol_epilogue(const std::vector<binary::decoded_insn_t> &insns, std::size_t call_idx,
                            std::uint32_t tee_local, std::uint32_t *out_read,
                            std::uint32_t *out_reset, std::size_t *out_cursor)
{
  std::size_t k = call_idx + 1;
  if ( k < insns.size() && (insns[k].opcode == 0x21 || insns[k].opcode == 0x1A) )
    ++k; // invoke result local.set / drop (optional)
  // read: local.get T ; i32.load a=2 off=0 ; local.set V
  if ( k + 2 >= insns.size() || insns[k].opcode != 0x20
       || std::uint32_t(insns[k].imm[0]) != tee_local || insns[k + 1].opcode != 0x28
       || insns[k + 1].imm[0] != 2 || insns[k + 1].imm[1] != 0 || insns[k + 2].opcode != 0x21 )
    return false;
  *out_read = insns[k + 1].offset;
  k += 3;
  // reset: local.get T ; i32.const 0 ; i32.store a=2 off=0
  if ( k + 2 < insns.size() && insns[k].opcode == 0x20
       && std::uint32_t(insns[k].imm[0]) == tee_local && insns[k + 1].opcode == 0x41
       && insns[k + 1].imm[0] == 0 && insns[k + 2].opcode == 0x36 && insns[k + 2].imm[0] == 2
       && insns[k + 2].imm[1] == 0 )
  {
    *out_reset = insns[k + 2].offset;
    k += 3;
  }
  else
  {
    *out_reset = UINT32_MAX;
  }
  *out_cursor = k;
  return true;
}

// The guard after the epilogue: `local.get V ; i32.const 1 ; i32.eq` then
// either `if` (inline pad in the then-branch) or `br_if` (remote pad via a
// label). Returns the guard insn index (or SIZE_MAX).
std::size_t find_guard_insn(const std::vector<binary::decoded_insn_t> &insns, std::size_t from,
                            std::uint32_t read_local, const std::uint8_t **out_kind)
{
  for ( std::size_t k = from; k + 3 < insns.size() && k < from + 8; ++k )
  {
    if ( insns[k].opcode == 0x20 && std::uint32_t(insns[k].imm[0]) == read_local
         && insns[k + 1].opcode == 0x41 && insns[k + 1].imm[0] == 1
         && (insns[k + 2].opcode == 0x46 || insns[k + 2].opcode == 0x47)
         && (insns[k + 3].opcode == 0x04 || insns[k + 3].opcode == 0x0D) )
    {
      if ( out_kind != nullptr )
        *out_kind = &insns[k + 3].opcode != nullptr ? nullptr : nullptr; // kind unused
      return k + 3;
    }
  }
  return SIZE_MAX;
}

// Prove a REMOTE pad (the br_if target block): catch call near the block
// start, cleanup of calls/stores only, resume + unreachable at the end
// before the closing `end`.
bool prove_remote_pad(const std::vector<binary::decoded_insn_t> &insns, std::uint32_t target_off,
                      const std::set<std::uint32_t> &catch_fns,
                      const std::set<std::uint32_t> &resume_fns, eh_pad_ir_t *out)
{
  std::size_t start = SIZE_MAX;
  for ( std::size_t q = 0; q < insns.size(); ++q )
    if ( insns[q].offset >= target_off )
    {
      start = q;
      break;
    }
  if ( start == SIZE_MAX )
    return false;
  // find the catch call within the first few statements of the block
  std::size_t catch_idx = SIZE_MAX;
  for ( std::size_t q = start; q < insns.size() && q < start + 8; ++q )
    if ( insns[q].opcode == 0x10 && catch_fns.count(std::uint32_t(insns[q].imm[0])) != 0 )
    {
      catch_idx = q;
      break;
    }
  if ( catch_idx == SIZE_MAX )
    return false;
  out->catch_offset = insns[catch_idx].offset;
  out->catch_fn = std::uint32_t(insns[catch_idx].imm[0]);
  out->is_catch_all = false;
  std::size_t k = catch_idx + 1;
  if ( k < insns.size() && insns[k].opcode == 0x21 )
    ++k; // local.set E
  std::uint32_t cleanup = 0;
  while ( k + 1 < insns.size() )
  {
    if ( insns[k].opcode == 0x0B )
      return false; // block end without resume
    if ( insns[k].opcode == 0x20 && insns[k + 1].opcode == 0x10
         && resume_fns.count(std::uint32_t(insns[k + 1].imm[0])) != 0 )
      break;
    if ( insns[k].opcode == 0x10 )
      ++cleanup;
    ++k;
    if ( cleanup > 64 || k - catch_idx > 300 )
      return false;
  }
  if ( k + 1 >= insns.size() )
    return false;
  out->resume_offset = insns[k + 1].offset;
  k += 2;
  if ( k >= insns.size() || insns[k].opcode != 0x00 )
    return false;
  out->trap_offset = insns[k].offset;
  out->cleanup_count = cleanup;
  out->pad_offset = insns[start].offset;
  return true;
}

// Prove an INLINE pad (the guard's then-branch):
//   [call catch][local.set E] <cleanup: calls / sp stores only>
//   [local.get E][call resume] unreachable end
bool prove_inline_pad(const std::vector<binary::decoded_insn_t> &insns, std::size_t if_idx,
                      const std::set<std::uint32_t> &catch_fns,
                      const std::set<std::uint32_t> &resume_fns, eh_pad_ir_t *out)
{
  std::size_t k = if_idx + 1;
  if ( k >= insns.size() || insns[k].opcode != 0x10
       || catch_fns.count(std::uint32_t(insns[k].imm[0])) == 0 )
    return false;
  out->catch_offset = insns[k].offset;
  out->catch_fn = std::uint32_t(insns[k].imm[0]);
  out->is_catch_all = false; // set by the caller via catch_all_fns
  ++k;
  if ( k < insns.size() && insns[k].opcode == 0x21 )
    ++k; // local.set E
  std::uint32_t cleanup = 0;
  while ( k + 1 < insns.size()
          && !(insns[k].opcode == 0x20 && k + 1 < insns.size() && insns[k + 1].opcode == 0x10
               && resume_fns.count(std::uint32_t(insns[k + 1].imm[0]))) )
  {
    // cleanup statements: calls (with their arg pushes), plain stores
    if ( insns[k].opcode == 0x10 )
      ++cleanup;
    if ( insns[k].opcode == 0x0B ) // fell out of the then-branch
      return false;
    ++k;
    if ( cleanup > 64 || k - if_idx > 300 )
      return false;
  }
  if ( k + 1 >= insns.size() )
    return false;
  out->resume_offset = insns[k + 1].offset;
  k += 2;
  if ( k >= insns.size() || insns[k].opcode != 0x00 )
    return false;
  out->trap_offset = insns[k].offset;
  out->cleanup_count = cleanup;
  out->pad_offset = insns[if_idx + 1].offset;
  return true;
}

} // namespace

void build_eh_function_irs(module_t &module)
{
  // Identity sets (the loader owns recognition now):
  //  - invoke_* imports + bytecode-proven legalize-i64 wrappers around them
  //  - catch/resume/trap callees by import/export name
  //  - the __THREW__ flag globals via the GOT naming resolution
  std::set<std::uint32_t> invoke_fns;
  std::set<std::uint32_t> catch_fns;
  std::set<std::uint32_t> resume_fns;
  std::set<std::uint32_t> catch_all_fns;
  for ( std::size_t i = 0; i < module.function_imports.size(); ++i )
  {
    const std::string &n = module.function_imports[i].name;
    // collector identity takes every invoke_* import (the strict [vijfd]
    // charset check is a RENDER concern; the loader trusts the import table)
    if ( n.rfind("invoke_", 0) == 0 && n.size() > 7 )
      invoke_fns.insert(std::uint32_t(i));
    else if ( is_find_matching_catch_name(n.c_str()) )
    {
      catch_fns.insert(std::uint32_t(i));
      if ( is_find_matching_catch_2_name(n.c_str()) )
        catch_all_fns.insert(std::uint32_t(i));
    }
    else if ( is_resume_exception_name(n.c_str()) )
      resume_fns.insert(std::uint32_t(i));
  }
  const std::size_t imports = module.imported_func_types.size();
  for ( const export_t &e : module.function_names )
  {
    if ( e.index < imports )
      continue;
    if ( is_find_matching_catch_name(e.name.c_str()) )
    {
      catch_fns.insert(e.index);
      if ( is_find_matching_catch_2_name(e.name.c_str()) )
        catch_all_fns.insert(e.index);
    }
    else if ( is_resume_exception_name(e.name.c_str()) )
      resume_fns.insert(e.index);
  }
  // Legalize-i64 wrappers (defined bodies forwarding to an invoke import).
  std::map<std::uint32_t, std::uint32_t> wrapper_to_invoke;
  for ( std::size_t i = 0; i < module.bodies.size(); ++i )
  {
    const body_t &body = module.bodies[i];
    if ( body.offset >= module.code.size() || body.size < 14 || body.size > 256 )
      continue;
    const std::size_t avail
      = std::min<std::size_t>(body.size, module.code.size() - std::size_t(body.offset));
    std::uint32_t callee = UINT32_MAX;
    if ( match_legalize_i64_wrapper_body(module.code.data() + body.offset, avail, &callee)
         && invoke_fns.count(callee) != 0 )
    {
      wrapper_to_invoke.emplace(std::uint32_t(imports + i), callee);
      invoke_fns.insert(std::uint32_t(imports + i));
    }
  }
  // __THREW__ flag globals: GOT naming resolution over the start function
  // (index -> "GOT.mem.__THREW__"-style name; also accept a direct import
  // named __THREW__/_THREW__ for non-GOT builds).
  std::set<std::uint32_t> threw_globals;
  {
    std::unordered_map<std::uint32_t, std::string> got_names;
    resolve_got_globals(module, &got_names);
    for ( const auto &kv : got_names )
    {
      const std::string &n = kv.second;
      if ( n.find("__THREW__") != std::string::npos || n.find("_THREW__") != std::string::npos )
        threw_globals.insert(kv.first);
    }
    for ( std::size_t i = 0; i < module.globals.size(); ++i )
    {
      const global_t &g = module.globals[i];
      if ( g.imported && (g.field_name == "__THREW__" || g.field_name == "_THREW__") )
        threw_globals.insert(std::uint32_t(i));
    }
  }
  if ( invoke_fns.empty() || threw_globals.empty() )
    return; // not an Emscripten-EH module (fail closed, no marks)

  for ( std::size_t bi = 0; bi < module.bodies.size(); ++bi )
  {
    body_t &body = module.bodies[bi];
    if ( body.offset >= module.code.size() || body.size == 0 )
      continue;
    const uchar *p = module.code.data() + body.offset;
    const std::size_t avail
      = std::min<std::size_t>(body.size, module.code.size() - std::size_t(body.offset));
    binary::insn_cursor_t cursor{ p, avail, 0, nullptr };
    std::vector<binary::decoded_insn_t> insns;
    binary::decoded_insn_t d;
    while ( cursor.pos < avail && binary::next_insn(cursor, &d) )
      insns.push_back(d);

    eh_function_ir_t ir;
    ir.present = false;
    for ( std::size_t k = 0; k < insns.size() && ir.invokes.size() < kEhMaxInvokeSites; ++k )
    {
      if ( insns[k].opcode != 0x10 || invoke_fns.count(std::uint32_t(insns[k].imm[0])) == 0 )
        continue;
      const auto wrap_it = wrapper_to_invoke.find(std::uint32_t(insns[k].imm[0]));
      const std::uint32_t wrapped_fn
        = wrap_it != wrapper_to_invoke.end() ? wrap_it->second : UINT32_MAX;
      zero_store_hit_t prologue;
      if ( !find_protocol_prologue(insns, k, threw_globals, &prologue) )
      {
        eh_invoke_site_ir_t identity;
        identity.call_offset = insns[k].offset;
        identity.wrapped_invoke_fn = wrapped_fn;
        if ( ir.invokes.size() < kEhMaxInvokeSites )
        {
          ir.invokes.push_back(identity);
          ir.present = true;
        }
        continue;
      }
      std::uint32_t read_off = UINT32_MAX;
      std::uint32_t reset_off = UINT32_MAX;
      std::size_t after = 0;
      eh_invoke_site_ir_t site_guard; // carries guard/pad offsets when the
                                      // epilogue (and thus the guard) is found
      if ( find_protocol_epilogue(insns, k, prologue.tee_local, &read_off, &reset_off, &after) )
      {
        // guard + pad (inline `if` form or remote `br_if` form)
        const std::uint32_t read_local = std::uint32_t(insns[after - 1].imm[0]);
        const std::size_t guard_idx = find_guard_insn(insns, after, read_local, nullptr);
        if ( guard_idx != SIZE_MAX )
        {
          eh_pad_ir_t pad;
          bool pad_ok = false;
          if ( insns[guard_idx].opcode == 0x04 )
          {
            pad_ok = prove_inline_pad(insns, guard_idx, catch_fns, resume_fns, &pad);
          }
          else // 0x0D br_if: resolve the target through the control records
          {
            const body_t::control_t *ctrl = nullptr;
            for ( const body_t::control_t &candidate : body.control )
              if ( candidate.opcode == 0x0D && candidate.offset == insns[guard_idx].offset )
              {
                ctrl = &candidate;
                break;
              }
            if ( ctrl != nullptr && !ctrl->targets.empty()
                 && ctrl->targets[0] > insns[guard_idx].offset )
              pad_ok = prove_remote_pad(insns, ctrl->targets[0], catch_fns, resume_fns, &pad);
          }
          if ( pad_ok && pad.pad_valid(body.size) )
          {
            pad.is_catch_all = catch_all_fns.count(pad.catch_fn) != 0;
            site_guard.pad_offset = pad.pad_offset;
            if ( ir.pads.size() < kEhMaxPads )
              ir.pads.push_back(pad);
          }
          site_guard.guard_br_offset = insns[guard_idx].offset;
        }
      }
      // else: the noreturn-throw path — the invoke callee never returns, the
      // body continues with `drop; br` and there is no flag read to guard on.
      eh_invoke_site_ir_t site;
      site.call_offset = insns[k].offset;
      site.threw_global = prologue.threw_global;
      site.tee_local = prologue.tee_local;
      site.tee_offset = prologue.tee_offset;
      site.zero_store_before = prologue.store_offset;
      site.read_offset = read_off;
      site.zero_store_after = reset_off;
      site.guard_br_offset = site_guard.guard_br_offset;
      site.pad_offset = site_guard.pad_offset;
      site.wrapped_invoke_fn = wrapped_fn;
      if ( !site.invoke_site_valid(body.size) )
        continue;
      ir.invokes.push_back(site);
      ir.present = true;
    }
    if ( ir.present )
    {
      std::sort(ir.invokes.begin(), ir.invokes.end(),
                [](const eh_invoke_site_ir_t &a, const eh_invoke_site_ir_t &b)
                { return a.call_offset < b.call_offset; });
      std::sort(ir.pads.begin(), ir.pads.end(), [](const eh_pad_ir_t &a, const eh_pad_ir_t &b)
                { return a.pad_offset < b.pad_offset; });
      body.eh_ir = std::move(ir);
    }
  }
}

} // namespace wasmhex::loader
