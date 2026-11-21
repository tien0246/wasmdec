// libc++ SSO dtor evidence: the loader-side builder (core sources; the
// processor only consumes blob 'l' via the codec in libcxx.cpp).
#include "libcxx.hpp"

#include "../../binary/instruction_stream.hpp"
#include "../../types.hpp"

#include <algorithm>
#include <set>
#include <string>

namespace wasmhex::loader
{

namespace
{

// One candidate tag-guard position during the scan.
struct tag_guard_hit_t
{
  std::size_t tag_idx = 0;               // the signed tag load
  std::size_t if_idx = 0;                // the `if`
  std::uint32_t base_local = UINT32_MAX; // pointer form: the local p
  std::uint32_t sp_global = UINT32_MAX;  // stack form: the SP global
  std::uint64_t sp_tag_off = 0;          // stack form: memarg of the tag load
  std::uint64_t sp_data_off = 0;         // stack form: memarg of the data load
  libcxx_sso_form_t form = libcxx_sso_form_t::pointer_tag8;
};

// The tag guard: `<addr> ; i32.load8_s [p+11] | i32.load [p+8] ;
// i32.const 0 ; i32.lt_s ; if`. The address is `local.get p` (pointer
// form) or `global.get SP` (stack form; the data load repeats it and the
// memarg delta must be 11 for a 1-byte tag, 8 for a 4-byte tag).
bool find_tag_guard(const std::vector<binary::decoded_insn_t> &insns, std::size_t k,
                    std::uint32_t sp_global, tag_guard_hit_t *out)
{
  if ( k + 3 >= insns.size() )
    return false;
  const bool tag8 = insns[k].opcode == 0x2C;  // i32.load8_s
  const bool tag32 = insns[k].opcode == 0x28; // i32.load
  if ( !tag8 && !tag32 )
    return false;
  if ( k < 1 )
    return false;
  // pointer form: local.get p immediately before, memarg 11 (1B) / 8 (4B)
  if ( insns[k - 1].opcode == 0x20 )
  {
    if ( !(insns[k + 1].opcode == 0x41 && insns[k + 1].imm[0] == 0 && insns[k + 2].opcode == 0x48
           && insns[k + 3].opcode == 0x04) )
      return false;
    if ( tag8 && insns[k].imm[1] != 11 )
      return false;
    if ( tag32 && !(insns[k].imm[0] == 2 && insns[k].imm[1] == 8) )
      return false;
    out->base_local = std::uint32_t(insns[k - 1].imm[0]);
    out->form = tag8 ? libcxx_sso_form_t::pointer_tag8 : libcxx_sso_form_t::pointer_tag32;
    out->tag_idx = k;
    out->if_idx = k + 3;
    return true;
  }
  // stack form: global.get SP immediately before; the delta check happens
  // at the free-argument load (both memargs must be known by then)
  if ( sp_global != UINT32_MAX && insns[k - 1].opcode == 0x23
       && std::uint32_t(insns[k - 1].imm[0]) == sp_global )
  {
    if ( !(insns[k + 1].opcode == 0x41 && insns[k + 1].imm[0] == 0 && insns[k + 2].opcode == 0x48
           && insns[k + 3].opcode == 0x04) )
      return false;
    out->sp_global = sp_global;
    out->sp_tag_off = insns[k].imm[1];
    out->form = tag8 ? libcxx_sso_form_t::stack_tag8 : libcxx_sso_form_t::stack_tag32;
    out->base_local = UINT32_MAX;
    out->tag_idx = k;
    out->if_idx = k + 3;
    return true;
  }
  return false;
}

} // namespace

void build_libcxx_function_irs(module_t &module)
{
  // `free` identity: an import or an export literally named "free" (leading
  // underscores tolerated, mirroring the ctree layer's name check).
  std::set<std::uint32_t> free_fns;
  for ( std::size_t i = 0; i < module.function_imports.size(); ++i )
    if ( is_free_callee_name(module.function_imports[i].name.c_str()) )
      free_fns.insert(std::uint32_t(i));
  const std::size_t imports = module.imported_func_types.size();
  for ( const export_t &e : module.function_exports )
    if ( e.index >= imports && is_free_callee_name(e.name.c_str()) )
      free_fns.insert(e.index);
  if ( free_fns.empty() )
    return; // not an Emscripten libc++ module (fail closed, no marks)

  for ( std::size_t bi = 0; bi < module.bodies.size(); ++bi )
  {
    body_t &body = module.bodies[bi];
    if ( body.offset >= module.code.size() || body.size == 0 )
      continue;
    const std::uint32_t sp_global = body.has_c_frame ? body.c_frame.sp_global_index : UINT32_MAX;
    const std::size_t avail
      = std::min<std::size_t>(body.size, module.code.size() - std::size_t(body.offset));
    binary::insn_cursor_t cursor{ module.code.data() + body.offset, avail, 0, nullptr };
    std::vector<binary::decoded_insn_t> insns;
    binary::decoded_insn_t d;
    while ( cursor.pos < avail && binary::next_insn(cursor, &d) )
      insns.push_back(d);

    libcxx_function_ir_t ir;
    ir.present = false;
    for ( std::size_t k = 0; k + 1 < insns.size() && ir.sso_dtors.size() < kLibcxxMaxSsoDtors; ++k )
    {
      tag_guard_hit_t guard;
      if ( !find_tag_guard(insns, k, sp_global, &guard) )
        continue;
      // The guarded branch must be exactly the free call on the string data:
      //   pointer: local.get p ; i32.load [p+0] ; call <free>
      //   stack:   global.get SP ; i32.load [SP+Kd] ; call <free>
      std::size_t q = guard.if_idx + 1;
      if ( q + 2 >= insns.size() )
        continue;
      if ( guard.form == libcxx_sso_form_t::pointer_tag8
           || guard.form == libcxx_sso_form_t::pointer_tag32 )
      {
        if ( !(insns[q].opcode == 0x20 && std::uint32_t(insns[q].imm[0]) == guard.base_local
               && insns[q + 1].opcode == 0x28 && insns[q + 1].imm[0] == 2
               && insns[q + 1].imm[1] == 0) )
          continue;
      }
      else
      {
        if ( !(insns[q].opcode == 0x23 && std::uint32_t(insns[q].imm[0]) == guard.sp_global
               && insns[q + 1].opcode == 0x28 && insns[q + 1].imm[0] == 2) )
          continue;
        guard.sp_data_off = insns[q + 1].imm[1];
        const std::int64_t delta = std::int64_t(guard.sp_tag_off) - std::int64_t(guard.sp_data_off);
        const bool tag8 = guard.form == libcxx_sso_form_t::stack_tag8;
        if ( !(tag8 ? delta == 11 : delta == 8) )
          continue;
      }
      const std::size_t call_idx = q + 2;
      if ( insns[call_idx].opcode != 0x10
           || free_fns.count(std::uint32_t(insns[call_idx].imm[0])) == 0 )
        continue;

      libcxx_sso_dtor_ir_t site;
      site.guard_offset = insns[guard.if_idx].offset;
      site.tag_load_offset = insns[guard.tag_idx].offset;
      site.free_call_offset = insns[call_idx].offset;
      site.free_fn = std::uint32_t(insns[call_idx].imm[0]);
      site.form = std::uint8_t(guard.form);
      if ( !site.valid(body.size) )
        continue;
      ir.sso_dtors.push_back(site);
      ir.present = true;
    }
    if ( ir.present )
    {
      std::sort(ir.sso_dtors.begin(), ir.sso_dtors.end(),
                [](const libcxx_sso_dtor_ir_t &a, const libcxx_sso_dtor_ir_t &b)
                { return a.guard_offset < b.guard_offset; });
      body.libcxx_ir = std::move(ir);
    }
  }
}

} // namespace wasmhex::loader
