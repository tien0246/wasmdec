// The code-section emitter half of the old core/parse_code.cpp
// module_t::parse_code reads the code section, builds body_t records and
// rewrites the flat .text (virtual stubs, Go resume dispatches). Stays in
// WASMHEX_CORE_REST (not wasmhex_binary): it calls module_t::scan_control,
// which lives in the analysis library above binary.
#include "../types.hpp"
#include "../validation/function_validator.hpp"
#include "instructions.hpp"
#include "instruction_stream.hpp"

#include <algorithm>
#include <cstring>

namespace wasmhex::loader
{
bool module_t::parse_code(reader_t *r)
{
  if ( !resolve_c_stack_global() )
    return false;
  reader_t preflight = *r;
  uint64 preflight_count;
  if ( !preflight.vector_count(&preflight_count) || preflight_count != defined_func_types.size() )
    return false;
  for ( uint64 i = 0; i < preflight_count; ++i )
  {
    uint64 body_size, local_groups, local_count;
    if ( !preflight.uleb(&body_size) || body_size > preflight.size - preflight.pos )
      return false;
    reader_t expression = { preflight.data + preflight.pos, size_t(body_size) };
    preflight.pos += size_t(body_size);
    if ( !expression.vector_count(&local_groups) )
      return false;
    body_t body;
    for ( uint64 j = 0; j < local_groups; ++j )
    {
      uchar type;
      int64 heap_type;
      uchar ref_form;
      // Cap the per-function local count like a real engine (V8 allows
      // 50,000). Without the cap a ~30-byte module can declare 20 million
      // locals and the resizes below try to allocate hundreds of MB.
      constexpr uint64 max_function_locals = 50000;
      if ( !expression.uleb(&local_count) || local_count > max_module_size
           || !read_value_type(&expression, &type, &heap_type, &ref_form)
           || !validate_heap_type_index(heap_type) )
        return false;
      if ( uint64(body.local_types.size()) + local_count > max_function_locals )
      {
        reject(error_code::resource_local_count,
               "WebAssembly function exceeds the local count limit");
        failure_function_index = uint32(imported_func_types.size() + i);
        return false;
      }
      const size_t old_count = body.local_types.size();
      body.local_types.resize(old_count + size_t(local_count));
      body.local_heap_types.resize(old_count + size_t(local_count));
      body.local_ref_forms.resize(old_count + size_t(local_count));
      for ( size_t k = old_count; k < body.local_types.size(); ++k )
      {
        body.local_types[k] = type;
        body.local_heap_types[k] = heap_type;
        body.local_ref_forms[k] = ref_form;
      }
    }
    if ( defined_func_types[size_t(i)] >= types.size() )
    {
      reject(error_code::policy_other, "WebAssembly function body has an invalid type index");
      return false;
    }
    {
      const function_validation_t check = validate_function_body(
        *this, types[defined_func_types[size_t(i)]], body.local_types,
        expression.data + expression.pos, expression.size - expression.pos, &body.local_ref_forms);
      // Route by state. invalid/malformed/resource_limit reject the
      // module; unsupported is recorded as an explicit non-clearance (the
      // function index list lets consumers see exactly which functions were
      // never validated).
      if ( check.state != function_validation_state_t::valid
           && check.state != function_validation_state_t::unsupported )
      {
        reject(check.code, check.reason);
        failure_function_index = uint32(imported_func_types.size() + i);
        failure_body_offset = check.offset;
        return false;
      }
      if ( check.state == function_validation_state_t::unsupported )
      {
        ++functions_not_fully_validated;
        unsupported_function_indices.push_back(uint32(imported_func_types.size() + i));
        if ( first_validation_gap == nullptr )
          first_validation_gap = check.reason;
      }
    }
    if ( expression.pos == expression.size
         || !scan_control(&body, expression.data + expression.pos, expression.size - expression.pos,
                          types[defined_func_types[size_t(i)]]) )
    {
      if ( failure_reason == nullptr )
        reject(error_code::bad_instruction,
               "Native wasmhex rejected an unsupported or malformed function instruction");
      return false;
    }
  }
  if ( preflight.pos != preflight.size )
    return false;
  const bool fixed_extent
    = (!memory_shared && !memory_imported && !memory_exported && !uses_memory_grow)
    || (memory_has_declared_maximum && memory_declared_maximum_pages == memory_minimum_pages);
  fixed_memory32 = memory_count == 1 && !memory64 && fixed_extent;
  fixed_memory64 = memory_count == 1 && memory64 && fixed_extent;
  // A memory32 without a maximum is still bounded by its 32-bit address space.
  growable_memory_pages
    = memory_has_declared_maximum ? memory_declared_maximum_pages : max_growable_memory_pages;
  growable_memory32 = memory_count == 1 && !memory64 && !memory_shared
    && (memory_imported || memory_exported || uses_memory_grow)
    && growable_memory_pages > memory_minimum_pages
    && growable_memory_pages <= max_growable_memory_pages;
  growable_memory64 = memory_count == 1 && memory64 && !memory_shared && !memory_imported
    && !memory_exported && uses_memory_grow && memory_has_declared_maximum
    && memory_declared_maximum_pages > memory_minimum_pages
    && memory_declared_maximum_pages <= max_growable_memory_pages;
  uint64 count;
  if ( !r->vector_count(&count) )
    return false;
  bodies.reserve(size_t(count));
  for ( uint64 i = 0; i < count; ++i )
  {
    uint64 body_size;
    if ( !r->uleb(&body_size) || body_size > r->size - r->pos )
      return false;
    reader_t body = { r->data + r->pos, size_t(body_size) };
    const size_t section_body_start = r->pos;
    r->pos += size_t(body_size);
    if ( start_function_index != UINT32_MAX
         && uint32(imported_func_types.size() + i) == start_function_index )
    {
      start_function_body = body;
    }

    uint64 local_groups, local_count;
    if ( !body.vector_count(&local_groups) )
      return false;
    bodies.emplace_back();
    body_t &entry = bodies.back();
    for ( uint64 j = 0; j < local_groups; ++j )
    {
      uchar type;
      int64 heap_type;
      uchar ref_form;
      if ( !body.uleb(&local_count) || local_count > max_module_size
           || !read_value_type(&body, &type, &heap_type, &ref_form)
           || !validate_heap_type_index(heap_type) )
        return false;
      const size_t old_count = entry.local_types.size();
      entry.local_types.resize(old_count + size_t(local_count));
      entry.local_heap_types.resize(old_count + size_t(local_count));
      entry.local_ref_forms.resize(old_count + size_t(local_count));
      for ( size_t k = old_count; k < entry.local_types.size(); ++k )
      {
        entry.local_types[k] = type;
        entry.local_heap_types[k] = heap_type;
        entry.local_ref_forms[k] = ref_form;
      }
    }
    const size_t expression_size = body.size - body.pos;
    if ( expression_size == 0 || expression_size > UINT32_MAX )
      return false;
    // Code-section payload range of this function's body (right after the
    // body-size LEB, spanning local groups + opcodes). LLVM/lld emits DWARF
    // DW_AT_low_pc pointing exactly at body start; a low_pc pointing at the
    // first opcode still falls inside this range. See loader/dwarf.cpp.
    entry.source_offset = uint32(section_body_start);
    entry.source_end = uint32(section_body_start + body_size);
    if ( i >= defined_func_types.size() || defined_func_types[i] >= types.size() )
    {
      reject(error_code::policy_other, "WebAssembly function body has an invalid type index");
      return false;
    }
    if ( !scan_control(&entry, body.data + body.pos, expression_size,
                       types[defined_func_types[i]]) )
    {
      if ( failure_reason == nullptr )
        reject(error_code::bad_instruction,
               "Native wasmhex rejected an unsupported or malformed function instruction");
      return false;
    }
    {
      reader_t expression = { body.data + body.pos, expression_size };
      uchar opcode, end, throw_opcode;
      uint64 tag_index;
      if ( expression.byte(&opcode) && opcode == 0x08 && expression.uleb(&tag_index)
           && tag_index < tag_type_indices.size() && expression.byte(&end) && end == 0x0B
           && expression.pos == expression.size
           && types[tag_type_indices[size_t(tag_index)]].params.empty() )
      {
        entry.unconditional_throw_tag = uint32(tag_index);
        entry.unconditional_throw_payload_type = 0x40;
      }
      else
      {
        expression.pos = 0;
        if ( expression.byte(&opcode) && opcode == 0x20 && expression.uleb(&tag_index)
             && tag_index == 0 && expression.byte(&throw_opcode) && throw_opcode == 0x08
             && expression.uleb(&tag_index) && tag_index < tag_type_indices.size()
             && expression.byte(&end) && end == 0x0B && expression.pos == expression.size
             && entry.local_types.empty()
             && types[tag_type_indices[size_t(tag_index)]].params.size() == 1
             && (types[tag_type_indices[size_t(tag_index)]].params[0] == 0x7F
                 || types[tag_type_indices[size_t(tag_index)]].params[0] == 0x7E
                 || types[tag_type_indices[size_t(tag_index)]].params[0] == 0x7D
                 || types[tag_type_indices[size_t(tag_index)]].params[0] == 0x7C) )
        {
          entry.unconditional_throw_tag = uint32(tag_index);
          entry.unconditional_throw_payload_type
            = types[tag_type_indices[size_t(tag_index)]].params[0];
        }
      }
    }
    const bool has_in_function_throw
      = entry.eh_tries.size() == 1 && entry.eh_tries[0].throw_offset != UINT32_MAX;
    if ( entry.has_throw && entry.unconditional_throw_tag == UINT32_MAX && !has_in_function_throw )
    {
      reject(
        error_code::policy_eh_subset,
        "Native wasmhex supports direct throws only as one empty throw or local.get 0 followed by one scalar numeric payload throw");
      return false;
    }
    std::vector<uint32> tables;
    for ( size_t j = 0; j < entry.control.size(); ++j )
      if ( entry.control[j].opcode == 0x0E && entry.control[j].targets.size() > 1 )
        tables.push_back(uint32(j));

    entry.offset = code.size();
    // Jtbl v2: bytes of the virtual-stub region inserted right before
    // each br_table (used by the offset remapping below).
    size_t next_memory_access = 0;
    size_t cursor = 0;
    auto copy_range = [&](size_t from, size_t to)
    {
      if ( from > to || to > expression_size )
        return false;
      size_t copied = from;
      while ( next_memory_access < entry.memory_accesses.size()
              && entry.memory_accesses[next_memory_access].offset < to )
      {
        const body_t::memory_access_t &access = entry.memory_accesses[next_memory_access++];
        if ( access.offset < copied )
          return false;
        const size_t chunk_size = access.offset - copied;
        if ( chunk_size > max_module_size - code.size() )
          return false;
        const size_t old_size = code.size();
        code.resize(old_size + chunk_size);
        memcpy(code.data() + old_size, body.data + body.pos + copied, chunk_size);
        copied = access.offset;
        // No 0xF9 memory-check bytes are inserted into
        // the code stream any more — .text keeps the original bytes. The
        // edge metadata stays (access/check/trap EAs, memarg, width), with
        // check_offset anchored at the access itself.
        if ( has_memory_bounds_check() )
        {
          if ( !memory64 && access.memarg > UINT32_MAX )
            return false;
          entry.memory_trap_edges.emplace_back();
          body_t::memory_trap_edge_t &edge = entry.memory_trap_edges.back();
          edge.check_offset = access.offset;
          edge.access_offset = access.offset;
          edge.memarg = access.memarg;
          edge.store = access.store;
          edge.scalar = access.scalar;
        }
      }
      const size_t chunk_size = to - copied;
      if ( chunk_size > max_module_size - code.size() )
        return false;
      const size_t old_size = code.size();
      code.resize(old_size + chunk_size);
      memcpy(code.data() + old_size, body.data + body.pos + copied, chunk_size);
      return true;
    };
    for ( size_t table_number = 0; table_number < tables.size(); ++table_number )
    {
      const uint32 table_index = tables[table_number];
      body_t::control_t &control = entry.control[table_index];
      if ( control.after < cursor || control.after > expression_size )
        return false;
      // br_table no longer synthesizes a stub region in
      // .text — the case edges live entirely in the 'C'/'c'/'D' metadata
      // (source offsets; virtual == source for in-body offsets now that
      // nothing is inserted before the br_table). The br_table stays at
      // its original position and the flow enters it directly.
      if ( control.targets.size() > 1 )
      {
        if ( !copy_range(cursor, control.after) )
          return false;
        cursor = control.after;
        continue;
      }
      if ( !copy_range(cursor, control.after) )
        return false;
      if ( code.size() - entry.offset > UINT32_MAX )
        return false;
      cursor = control.after;
    }
    if ( !copy_range(cursor, expression_size)
         || next_memory_access != entry.memory_accesses.size() )
      return false;

    auto remap_offset = [](uint32 old_offset, uint32 *new_offset)
    {
      *new_offset = old_offset;
      return true;
    };
    for ( body_t::control_t &control : entry.control )
    {
      if ( !remap_offset(control.offset, &control.offset) )
        return false;
      for ( uint32 &target : control.targets )
        if ( !remap_offset(target, &target) )
          return false;
    }
    for ( body_t::resolved_call_t &call : entry.resolved_calls )
      if ( !remap_offset(call.offset, &call.offset)
           || ((call.sparse_table_call || call.reference_dispatch) && !call.tail_dispatch
               && !remap_offset(call.after, &call.after)) )
        return false;
    for ( body_t::eh_try_t &eh_try : entry.eh_tries )
    {
      auto remap_optional = [&](uint32 &offset)
      {
        return offset == UINT32_MAX || remap_offset(offset, &offset);
      };
      if ( !remap_offset(eh_try.offset, &eh_try.offset) || !remap_offset(eh_try.body, &eh_try.body)
           || !remap_offset(eh_try.end, &eh_try.end)
           || !remap_offset(eh_try.catch_offset, &eh_try.catch_offset)
           || !remap_offset(eh_try.handler_offset, &eh_try.handler_offset)
           || !remap_optional(eh_try.throw_offset) || !remap_optional(eh_try.payload_source_offset)
           || !remap_optional(eh_try.payload_source_after) || !remap_optional(eh_try.call_offset)
           || !remap_optional(eh_try.call_after) )
        return false;
    }
    if ( has_memory_bounds_check() )
      for ( body_t::memory_trap_edge_t &edge : entry.memory_trap_edges )
        if ( !remap_offset(edge.access_offset, &edge.access_offset) )
          return false;

    // The pending-stub materialization loop and the end-of-body
    // 0xF2 edge-stub loop are retired together — br_if / br_on_null /
    // br_on_non_null multi-value edges lower from metadata at the branch
    // instruction itself (emit_br / emit_br_on_reference run the spill and
    // merge moves ahead of the conditional jump), so no synthetic branch
    // bytes enter .text any more.

    for ( body_t::resolved_call_t &call : entry.resolved_calls )
    {
      // Call-dispatch stubs (0xF3/0xF4/0xF5/0xF6/0xF7/
      // 0xFA) no longer enter .text. The whole case table travels in the
      // packed 'j' blob stored at the call source EA (loader/entry.cpp)
      // and the decompiler builds the dispatch chain at the call site
      // (emit_dispatch_chain). Every stub coordinate stays UINT32_MAX.
      if ( !call.sparse_table_call && !call.reference_dispatch )
        continue;
      call.dispatch_offset = UINT32_MAX;
      call.default_offset = UINT32_MAX;
      call.bounds_check_offset = UINT32_MAX;
      call.bounds_offset = UINT32_MAX;
      for ( body_t::resolved_call_t::table_case_t &table_case : call.table_cases )
      {
        table_case.compare_offset = UINT32_MAX;
        table_case.stub_offset = UINT32_MAX;
      }
    }

    if ( has_memory_bounds_check() )
      for ( body_t::memory_trap_edge_t &edge : entry.memory_trap_edges )
      {
        if ( code.size() - entry.offset > UINT32_MAX || code.size() >= max_module_size )
          return false;
        edge.stub_offset = uint32(code.size() - entry.offset);
        code.push_back(0xF8);
      }

    const size_t stored_size = code.size() - entry.offset;
    if ( stored_size > UINT32_MAX )
      return false;
    entry.size = uint32(stored_size);
  }
  for ( body_t &caller : bodies )
  {
    if ( caller.eh_tries.empty() )
      continue;
    if ( caller.eh_tries.size() != 1 )
    {
      reject(error_code::policy_other, "Native wasmhex supports one legacy try/catch per function");
      return false;
    }
    body_t::eh_try_t &eh_try = caller.eh_tries[0];
    if ( eh_try.throw_offset != UINT32_MAX )
    {
      if ( caller.throw_count != 1 || eh_try.throw_tag_index != eh_try.tag_index
           || eh_try.call_offset != UINT32_MAX )
      {
        reject(
          error_code::policy_eh_subset,
          "Native wasmhex direct throw requires one legacy try with one throw and one matching catch");
        return false;
      }
      continue;
    }
    if ( eh_try.call_offset == UINT32_MAX && eh_try.has_unreachable )
    {
      reject(error_code::policy_eh_subset,
             "Native wasmhex keeps unreachable as a trap; it does not transfer to a legacy catch");
      return false;
    }
    const bool empty_call = eh_try.payload_type == 0x40
      && eh_try.payload_source_offset == UINT32_MAX && eh_try.call_offset == eh_try.body;
    const bool payload_call = (eh_try.payload_type == 0x7F || eh_try.payload_type == 0x7E
                               || eh_try.payload_type == 0x7D || eh_try.payload_type == 0x7C)
      && eh_try.payload_source_offset == eh_try.body
      && eh_try.call_offset == eh_try.payload_source_after;
    if ( eh_try.offset != 0 || eh_try.call_offset == UINT32_MAX
         || eh_try.function_index < imported_func_types.size()
         || eh_try.function_index >= imported_func_types.size() + bodies.size()
         || (!empty_call && !payload_call) || eh_try.call_after != eh_try.catch_offset )
    {
      reject(
        error_code::policy_eh_subset,
        "Native wasmhex legacy try must be the exact supported direct-call unwind shape or one matching in-function throw");
      return false;
    }
    const size_t callee_body_index = size_t(eh_try.function_index - imported_func_types.size());
    const body_t &callee = bodies[callee_body_index];
    const wasm_type_t &callee_type = types[defined_func_types[callee_body_index]];
    if ( callee.unconditional_throw_tag != eh_try.tag_index
         || callee.unconditional_throw_payload_type != eh_try.payload_type
         || !callee_type.results.empty()
         || (eh_try.payload_type == 0x40
               ? !callee_type.params.empty()
               : callee_type.params.size() != 1 || callee_type.params[0] != eh_try.payload_type) )
    {
      reject(
        error_code::policy_eh_subset,
        "Native wasmhex call-unwind requires a void callee with the exact signature for the matching empty or single-scalar-numeric tagged throw");
      return false;
    }
  }
  for ( size_t callee_index = 0; callee_index < bodies.size(); ++callee_index )
  {
    const body_t &callee = bodies[callee_index];
    if ( callee.unconditional_throw_tag == UINT32_MAX )
      continue;
    const uint32 function_index = uint32(imported_func_types.size() + callee_index);
    size_t protected_call_count = 0;
    for ( const body_t &caller : bodies )
      for ( const body_t::direct_call_t &call : caller.direct_calls )
        if ( call.function_index == function_index )
        {
          if ( caller.eh_tries.size() != 1 || caller.eh_tries[0].call_offset != call.offset
               || caller.eh_tries[0].function_index != function_index
               || caller.eh_tries[0].tag_index != callee.unconditional_throw_tag )
          {
            reject(
              error_code::policy_eh_subset,
              "Native wasmhex requires every call to an escaping-throw callee to have its matching supported legacy catch");
            return false;
          }
          ++protected_call_count;
        }
    if ( protected_call_count == 0 )
    {
      reject(
        error_code::policy_eh_subset,
        "Native wasmhex supports an escaping throw only as a direct callee of a matching supported legacy catch");
      return false;
    }
    const uint32 callee_type_index = defined_func_types[callee_index];
    for ( const body_t &caller : bodies )
      for ( const body_t::resolved_call_t &call : caller.resolved_calls )
      {
        bool may_target_thrower = false;
        if ( call.dynamic_table_call )
        {
          may_target_thrower = function_types_compatible(call.type_index, callee_type_index);
        }
        else if ( call.sparse_table_call || call.reference_dispatch )
        {
          may_target_thrower
            = std::any_of(call.table_cases.begin(), call.table_cases.end(),
                          [function_index](const body_t::resolved_call_t::table_case_t &table_case)
                          {
                            return table_case.trap == wasmhex_call_trap_t::none
                              && table_case.function_index == function_index;
                          });
        }
        else
        {
          may_target_thrower
            = call.trap == wasmhex_call_trap_t::none && call.function_index == function_index;
        }
        if ( may_target_thrower )
        {
          reject(
            error_code::policy_eh_subset,
            "Native wasmhex does not support indirect calls that may unwind from tagged throws");
          return false;
        }
      }
  }
  scan_go_provenance();
  scan_go_calls();
  return r->pos == r->size;
}

} // namespace wasmhex::loader
