#include <algorithm>
#include <limits>
#include "../types.hpp"
#include "../binary/instruction_stream.hpp"
#include "../validation/families/atomic_validator.hpp"
namespace wasmhex::loader
{
bool module_t::scan_control(body_t *body, const uchar *expression, size_t size,
                            const wasm_type_t &function_type)
{
  auto scalar_value_type = [](uchar type)
  {
    return type == 0x7F || type == 0x7E || type == 0x7D || type == 0x7C || type == 0x70
      || type == 0x6F || type == 0x71 || type == 0x72;
  };
  reader_t r = { expression, size };
  std::vector<control_frame_t> all_frames;
  std::vector<size_t> frames;
  control_frame_t function = {};
  function.kind = 0x00;
  function.start = 0;
  function.body = 0;
  function.label_types = function_type.results;
  function.label_heap_types = function_type.result_heap_types;
  function.label_ref_forms = function_type.result_ref_forms;
  all_frames.push_back(function);
  frames.push_back(0);
  std::vector<pending_branch_t> branches;
  uint32 function_end = UINT32_MAX;
  bool finished = false;
  struct constant_t
  {
    bool known = false;
    uchar type = 0x7F;
    uint64 value = 0;
  };
  std::vector<constant_t> abstract_stack;
  bool preceding_i31_ref = false;
  ref_fact_t preceding_ref_fact;
  const size_t parameter_count = function_type.params.size();
  const size_t local_count = parameter_count + body->local_types.size();
  struct c_stack_token_t
  {
    uchar opcode = 0;
    uint32 imm = 0;
  };
  std::vector<c_stack_token_t> c_stack_tokens;
  struct c_stack_epilogue_t
  {
    size_t set_sp_token_index = 0;
    int32 restore_delta = 0;
    uchar arith_opcode = 0;
    uint32 base_local = UINT32_MAX;
  };
  std::vector<c_stack_epilogue_t> c_stack_epilogues;
  size_t c_stack_get_count = 0;
  size_t c_stack_set_count = 0;
  bool c_stack_unrecognized_set = false;
  std::vector<uint32> local_write_counts;
  local_write_counts.resize(local_count);
  for ( size_t i = 0; i < local_count; ++i )
    local_write_counts[i] = 0;
  std::vector<uchar> local_constant_known;
  std::vector<uint64> local_constant_values;
  std::vector<ref_fact_t> local_ref_facts;
  local_constant_known.resize(local_count);
  local_constant_values.resize(local_count);
  local_ref_facts.resize(local_count);
  for ( size_t i = 0; i < local_count; ++i )
  {
    local_constant_known[i] = 0;
    local_constant_values[i] = 0;
    local_ref_facts[i] = ref_fact_t();
  }
  // shortcut: cap ref targets at 16 to bound generated stubs; raise after verifying larger dispatches.
  constexpr size_t max_ref_dispatch_targets = 16;
  auto merge_ref_facts = [&](const ref_fact_t &left, const ref_fact_t &right)
  {
    ref_fact_t merged;
    if ( !left.known || !right.known )
      return merged;
    merged.known = true;
    merged.targets = left.targets;
    for ( uint32 target : right.targets )
      if ( std::find(merged.targets.begin(), merged.targets.end(), target) == merged.targets.end() )
        merged.targets.push_back(target);
    std::sort(merged.targets.begin(), merged.targets.end());
    if ( merged.targets.empty() || merged.targets.size() > max_ref_dispatch_targets )
      return ref_fact_t();
    return merged;
  };
  // Keep ref provenance through if/else joins and clear it at other control-flow boundaries.
  auto clear_control_facts = [&](bool clear_ref_locals)
  {
    abstract_stack.clear();
    for ( size_t i = 0; i < local_count; ++i )
    {
      local_constant_known[i] = 0;
      if ( clear_ref_locals )
        local_ref_facts[i] = ref_fact_t();
    }
  };
  auto pop_constant = [&]()
  {
    constant_t value;
    if ( !abstract_stack.empty() )
    {
      value = abstract_stack.back();
      abstract_stack.pop_back();
    }
    return value;
  };
  auto resolve_table_cases
    = [&](body_t::resolved_call_t *call, uint32 table_index, uint32 type_index, uint32 table_size)
  {
    if ( table_size <= 4096 )
    {
      call->table_cases.resize(table_size);
      for ( uint32 slot_index = 0; slot_index < table_size; ++slot_index )
      {
        body_t::resolved_call_t::table_case_t &table_case = call->table_cases[slot_index];
        table_case.slot_index = slot_index;
        const table_slot_t *slot = find_table_slot(table_index, slot_index);
        if ( slot == nullptr )
          table_case.trap = wasmhex_call_trap_t::null_reference;
        else if ( !function_types_compatible(type_index, slot->function_type_index) )
          table_case.trap = wasmhex_call_trap_t::type_mismatch;
        else
        {
          table_case.function_index = slot->function_index;
          ++call->target_count;
        }
      }
      return true;
    }

    size_t slot_count = 0;
    for ( const table_slot_t &slot : table_slots )
      if ( slot.table_index == table_index )
        ++slot_count;
    if ( slot_count > WASMHEX_MAX_SPARSE_TABLE_DISPATCH_CASES )
    {
      reject(error_code::policy_other,
             "Native wasmhex indirect dispatch exceeds 65,536 initialized table slots");
      return false;
    }
    call->table_cases.reserve(slot_count);
    for ( const table_slot_t &slot : table_slots )
    {
      if ( slot.table_index != table_index )
        continue;
      call->table_cases.emplace_back();
      body_t::resolved_call_t::table_case_t &table_case = call->table_cases.back();
      table_case.slot_index = slot.slot_index;
      if ( !function_types_compatible(type_index, slot.function_type_index) )
        table_case.trap = wasmhex_call_trap_t::type_mismatch;
      else
      {
        table_case.function_index = slot.function_index;
        ++call->target_count;
      }
    }
    return true;
  };

  while ( r.pos < r.size )
  {
    const uint32 offset = uint32(r.pos);
    uchar opcode;
    if ( !r.byte(&opcode) )
      return false;
    if ( opcode == 0x00 )
      for ( size_t i = frames.size(); i != 0; --i )
      {
        control_frame_t &frame = all_frames[frames[i - 1]];
        if ( frame.kind == 0x06 )
        {
          if ( !frame.in_catch )
            body->eh_tries[frame.eh_try_index].has_unreachable = true;
          break;
        }
      }
    if ( opcode == 0x40 )
      uses_memory_grow = true;
    const bool previous_i31_ref = preceding_i31_ref;
    preceding_i31_ref = false;
    const ref_fact_t previous_ref_fact = preceding_ref_fact;
    preceding_ref_fact = ref_fact_t();
    const bool if_join = opcode == 0x04 || opcode == 0x05
      || (opcode == 0x0B && !frames.empty() && all_frames[frames.back()].kind == 0x04);
    if ( if_join )
      clear_control_facts(false);
    else if ( opcode == 0x00 || opcode == 0x02 || opcode == 0x03 || opcode == 0x06 || opcode == 0x07
              || opcode == 0x08 || opcode == 0x0B || opcode == 0x0C || opcode == 0x0D
              || opcode == 0x0E || opcode == 0x0F || opcode == 0xD5 || opcode == 0xD6 )
      clear_control_facts(true);
    if ( opcode != 0x20 && opcode != 0x21 && opcode != 0x22 && opcode != 0x23 && opcode != 0x24
         && opcode != 0x41 && opcode != 0x1A && !(opcode >= 0x6A && opcode <= 0x6C)
         && !(opcode >= 0x7C && opcode <= 0x7E) && !(opcode == 0x0B && frames.size() == 1) )
    {
      c_stack_tokens.push_back({ opcode, 0 });
    }
    if ( opcode == 0x02 || opcode == 0x03 || opcode == 0x04 || opcode == 0x06 )
    {
      if ( r.pos == r.size )
        return false;
      if ( opcode == 0x06
           && (r.data[r.pos] != 0x40
               || std::any_of(frames.begin(), frames.end(),
                              [&](size_t index) { return all_frames[index].kind == 0x06; })) )
      {
        reject(error_code::policy_other,
               "Native wasmhex EH supports one void, non-nested legacy try per function");
        return false;
      }
      control_frame_t frame = {};
      frame.kind = opcode;
      frame.start = offset;
      frame.body = uint32(r.pos);
      const uchar block_type = r.data[r.pos];
      if ( block_type == 0x63 || block_type == 0x64 )
      {
        uchar type;
        int64 heap_type;
        uchar ref_form;
        if ( !read_value_type(&r, &type, &heap_type, &ref_form)
             || !validate_heap_type_index(heap_type) )
          return false;
        frame.result_types.push_back(type);
        frame.result_heap_types.push_back(heap_type);
        frame.result_ref_forms.push_back(ref_form);
      }
      else if ( block_type == 0x40 || block_type == 0x7F || block_type == 0x7E || block_type == 0x7D
                || block_type == 0x7C || block_type == 0x70 || block_type == 0x6F
                || (block_type >= 0x6A && block_type <= 0x6D) )
      {
        uchar result_type = block_type;
        if ( block_type >= 0x6A && block_type <= 0x6D )
        {
          int64 heap_type;
          uchar ref_form;
          if ( !read_value_type(&r, &result_type, &heap_type, &ref_form) )
            return false;
          frame.result_heap_types.push_back(heap_type);
          frame.result_ref_forms.push_back(ref_form);
        }
        else
        {
          uchar ignored;
          if ( !r.byte(&ignored) )
            return false;
        }
        if ( block_type != 0x40 )
        {
          frame.result_types.push_back(result_type);
          if ( block_type < 0x6A || block_type > 0x6D )
          {
            frame.result_heap_types.push_back(no_heap_type);
            frame.result_ref_forms.push_back(0);
          }
        }
      }
      else
      {
        int64 type_index;
        if ( !r.sleb33(&type_index) || type_index < 0 || uint64(type_index) >= types.size() )
          return false;
        const wasm_type_t &signature = types[size_t(type_index)];
        for ( uchar type : signature.params )
        {
          if ( !scalar_value_type(type) )
            return false;
          frame.param_types.push_back(type);
        }
        frame.param_heap_types = signature.param_heap_types;
        frame.param_ref_forms = signature.param_ref_forms;
        for ( uchar type : signature.results )
        {
          if ( !scalar_value_type(type) )
            return false;
          frame.result_types.push_back(type);
        }
        frame.result_heap_types = signature.result_heap_types;
        frame.result_ref_forms = signature.result_ref_forms;
      }
      if ( opcode != 0x03 )
      {
        frame.label_types = frame.result_types;
        frame.label_heap_types = frame.result_heap_types;
        frame.label_ref_forms = frame.result_ref_forms;
      }
      if ( opcode == 0x04 )
        frame.ref_local_before = local_ref_facts;
      frame.body = uint32(r.pos);
      if ( opcode == 0x06 )
      {
        body->eh_tries.emplace_back();
        body_t::eh_try_t &eh_try = body->eh_tries.back();
        eh_try.offset = offset;
        eh_try.body = frame.body;
        frame.eh_try_index = body->eh_tries.size() - 1;
      }
      all_frames.push_back(frame);
      frames.push_back(all_frames.size() - 1);
      continue;
    }
    if ( opcode == 0x07 )
    {
      binary::insn_cursor_t cursor{ r.data, r.size, offset, nullptr };
      binary::decoded_insn_t decoded;
      if ( frames.size() <= 1 || all_frames[frames.back()].kind != 0x06
           || all_frames[frames.back()].in_catch || !binary::next_insn(cursor, &decoded)
           || decoded.imm[0] >= tag_type_indices.size() )
      {
        reject(error_code::policy_other,
               "Native wasmhex rejected an unmatched or unsupported legacy catch");
        return false;
      }
      r.pos = cursor.pos;
      const uint64 tag_index = decoded.imm[0];
      control_frame_t &frame = all_frames[frames.back()];
      const wasm_type_t &tag_type = types[tag_type_indices[size_t(tag_index)]];
      if ( !tag_type.results.empty() || tag_type.params.size() > 1
           || (!tag_type.params.empty() && tag_type.params[0] != 0x7F && tag_type.params[0] != 0x7E
               && tag_type.params[0] != 0x7D && tag_type.params[0] != 0x7C) )
      {
        reject(
          error_code::policy_eh_subset,
          "Native wasmhex legacy call-unwind supports empty tags or one scalar numeric payload only");
        return false;
      }
      body_t::eh_try_t &eh_try = body->eh_tries[frame.eh_try_index];
      eh_try.catch_offset = offset;
      eh_try.handler_offset = uint32(r.pos);
      eh_try.tag_index = uint32(tag_index);
      eh_try.payload_type = tag_type.params.empty() ? 0x40 : tag_type.params[0];
      frame.in_catch = true;
      continue;
    }
    if ( opcode == 0x08 )
    {
      binary::insn_cursor_t cursor{ r.data, r.size, offset, nullptr };
      binary::decoded_insn_t decoded;
      if ( !binary::next_insn(cursor, &decoded) || decoded.imm[0] >= tag_type_indices.size() )
      {
        reject(error_code::bad_eh, "Native wasmhex throw has an invalid tag index");
        return false;
      }
      r.pos = cursor.pos;
      const uint64 tag_index = decoded.imm[0];
      const wasm_type_t &tag_type = types[tag_type_indices[size_t(tag_index)]];
      if ( !tag_type.results.empty() || tag_type.params.size() > 1
           || (!tag_type.params.empty() && tag_type.params[0] != 0x7F && tag_type.params[0] != 0x7E
               && tag_type.params[0] != 0x7D && tag_type.params[0] != 0x7C) )
      {
        reject(error_code::policy_eh_subset,
               "Native wasmhex supports empty throws or one scalar numeric payload throw only");
        return false;
      }
      body->has_throw = true;
      if ( body->throw_count == UINT32_MAX )
        return false;
      ++body->throw_count;
      for ( size_t i = frames.size(); i != 0; --i )
      {
        const size_t frame_index = frames[i - 1];
        control_frame_t &frame = all_frames[frame_index];
        if ( frame.kind != 0x06 )
          continue;
        if ( frame.in_catch )
          break;
        body_t::eh_try_t &eh_try = body->eh_tries[frame.eh_try_index];
        if ( eh_try.throw_offset != UINT32_MAX )
        {
          reject(error_code::policy_other,
                 "Native wasmhex supports one direct throw per legacy try");
          return false;
        }
        eh_try.throw_offset = offset;
        eh_try.throw_tag_index = uint32(tag_index);
        break;
      }
      clear_control_facts(true);
      continue;
    }
    if ( opcode == 0x09 || opcode == 0x0A || opcode == 0x19 || opcode == 0x1F )
    {
      reject(
        error_code::policy_eh_subset,
        "Native wasmhex EH supports one typed legacy catch and direct empty or one scalar numeric payload throw");
      return false;
    }
    if ( opcode == 0x05 )
    {
      if ( frames.size() <= 1 || all_frames[frames.back()].kind != 0x04
           || all_frames[frames.back()].otherwise != UINT32_MAX )
        return false;
      control_frame_t &frame = all_frames[frames.back()];
      frame.ref_local_then = local_ref_facts;
      frame.has_else = true;
      local_ref_facts = frame.ref_local_before;
      frame.otherwise = offset;
      continue;
    }
    if ( opcode == 0x0B )
    {
      if ( frames.empty() )
        return false;
      const size_t frame_index = frames.back();
      all_frames[frame_index].end = offset;
      control_frame_t frame = all_frames[frame_index];
      if ( frame.kind == 0x04 )
      {
        const std::vector<ref_fact_t> &then_facts
          = frame.has_else ? frame.ref_local_then : local_ref_facts;
        const std::vector<ref_fact_t> &else_facts
          = frame.has_else ? local_ref_facts : frame.ref_local_before;
        for ( size_t i = 0; i < local_count; ++i )
          local_ref_facts[i] = merge_ref_facts(then_facts[i], else_facts[i]);
      }
      frames.pop_back();
      if ( frame.kind == 0x00 )
      {
        if ( !frames.empty() || r.pos != r.size )
          return false;
        c_stack_tokens.push_back({ opcode, 0 });
        function_end = offset;
        body_t::control_t marker;
        marker.opcode = 0x0B;
        marker.offset = offset;
        body->control.push_back(marker);
        finished = true;
        break;
      }
      if ( frame.kind == 0x04 && !frame.result_types.empty() && frame.otherwise == UINT32_MAX )
        return false;
      if ( frame.kind == 0x06 )
      {
        body_t::eh_try_t &eh_try = body->eh_tries[frame.eh_try_index];
        if ( eh_try.catch_offset == UINT32_MAX )
        {
          reject(error_code::policy_other, "Native wasmhex legacy try requires one typed catch");
          return false;
        }
        eh_try.end = offset;
      }
      body_t::control_t scope_end;
      scope_end.opcode = frame.kind;
      scope_end.offset = frame.start;
      scope_end.targets.push_back(offset);
      scope_end.params = frame.param_types;
      scope_end.types = frame.result_types;
      scope_end.labels = frame.label_types;
      scope_end.param_heap_types = frame.param_heap_types;
      scope_end.type_heap_types = frame.result_heap_types;
      scope_end.label_heap_types = frame.label_heap_types;
      scope_end.param_ref_forms = frame.param_ref_forms;
      scope_end.type_ref_forms = frame.result_ref_forms;
      scope_end.label_ref_forms = frame.label_ref_forms;
      scope_end.scope_end = true;
      body->control.push_back(scope_end);
      if ( frame.kind == 0x04 )
      {
        body_t::control_t if_control;
        if_control.opcode = 0x04;
        if_control.offset = frame.start;
        if_control.targets.push_back(frame.otherwise == UINT32_MAX ? offset + 1 : frame.otherwise);
        body->control.push_back(if_control);
        if ( frame.otherwise != UINT32_MAX )
        {
          body_t::control_t else_control;
          else_control.opcode = 0x05;
          else_control.offset = frame.otherwise;
          else_control.targets.push_back(offset + 1);
          body->control.push_back(else_control);
        }
      }
      continue;
    }
    if ( opcode == 0x10 || opcode == 0x12 )
    {
      binary::insn_cursor_t cursor{ r.data, r.size, offset, nullptr };
      binary::decoded_insn_t decoded;
      if ( !binary::next_insn(cursor, &decoded)
           || decoded.imm[0] >= imported_func_types.size() + defined_func_types.size() )
        return false;
      r.pos = cursor.pos;
      const uint64 function_index = decoded.imm[0];
      body->direct_calls.emplace_back();
      body_t::direct_call_t &direct_call = body->direct_calls.back();
      direct_call.offset = offset;
      direct_call.function_index = uint32(function_index);
      if ( opcode == 0x12 )
      {
        if ( std::any_of(frames.begin(), frames.end(),
                         [&](size_t index) { return all_frames[index].kind == 0x06; }) )
        {
          reject(error_code::policy_eh_subset,
                 "Native wasmhex does not support tail calls inside legacy try/catch");
          return false;
        }
        abstract_stack.clear();
        continue;
      }
      uint32 callee_type_index = 0;
      if ( !function_type_index(function_index, &callee_type_index) )
        return false;
      body->max_tuple_call_bytes = std::max(
        body->max_tuple_call_bytes, wasm_results_byte_size(types[callee_type_index].results));
      if ( std::any_of(frames.begin(), frames.end(), [&](size_t index)
                       { return all_frames[index].kind == 0x06 && all_frames[index].in_catch; }) )
      {
        reject(error_code::policy_eh_subset,
               "Native wasmhex does not support calls inside legacy catch handlers");
        return false;
      }
      if ( !frames.empty() && all_frames[frames.back()].kind == 0x06
           && !all_frames[frames.back()].in_catch )
      {
        control_frame_t &frame = all_frames[frames.back()];
        body_t::eh_try_t &eh_try = body->eh_tries[frame.eh_try_index];
        const bool call_at_entry = offset == eh_try.body;
        const bool call_after_payload_local_get
          = eh_try.payload_source_offset == eh_try.body && eh_try.payload_source_after == offset;
        if ( eh_try.call_offset != UINT32_MAX || (!call_at_entry && !call_after_payload_local_get) )
        {
          reject(
            error_code::policy_eh_subset,
            "Native wasmhex EH requires one direct call at try entry, with at most one scalar numeric local.get payload source");
          return false;
        }
        eh_try.call_offset = offset;
        eh_try.call_after = uint32(r.pos);
        eh_try.function_index = uint32(function_index);
        if ( uint32(r.pos) != eh_try.catch_offset && eh_try.catch_offset != UINT32_MAX )
        {
          reject(error_code::policy_other,
                 "Native wasmhex EH requires the direct call to end the try body");
          return false;
        }
      }
      abstract_stack.clear();
      continue;
    }
    if ( opcode == 0x0C || opcode == 0x0D || opcode == 0x0E )
    {
      body_t::control_t control;
      control.opcode = opcode;
      control.offset = offset;
      const size_t branch_index = body->control.size();
      body->control.push_back(control);
      pending_branch_t branch;
      branch.control_index = branch_index;
      uint64 count = 1;
      if ( opcode == 0x0E )
      {
        if ( !r.uleb(&count) || count >= UINT32_MAX || count + 1 > r.size - r.pos )
          return false;
      }
      for ( uint64 j = 0; j < count + (opcode == 0x0E ? 1 : 0); ++j )
      {
        uint64 depth;
        if ( !r.uleb(&depth) || depth >= frames.size() )
          return false;
        const size_t frame_index = frames[frames.size() - 1 - size_t(depth)];
        const control_frame_t &target_frame = all_frames[frame_index];
        if ( j == 0 )
        {
          body->control[branch_index].types = target_frame.label_types;
          body->control[branch_index].type_heap_types = target_frame.label_heap_types;
          body->control[branch_index].type_ref_forms = target_frame.label_ref_forms;
        }
        else if ( body->control[branch_index].types != target_frame.label_types
                  || body->control[branch_index].type_heap_types != target_frame.label_heap_types
                  || body->control[branch_index].type_ref_forms != target_frame.label_ref_forms )
          return false;
        branch.target_frames.push_back(frame_index);
        body->control[branch_index].depths.push_back(uint32(depth));
      }
      if ( opcode == 0x0E )
        body->control[branch_index].after = uint32(r.pos);
      branches.push_back(branch);
      continue;
    }
    if ( opcode == 0xD0 )
    {
      binary::insn_cursor_t cursor{ r.data, r.size, offset, nullptr };
      binary::decoded_insn_t decoded;
      const int64 heap_type = binary::next_insn(cursor, &decoded) ? int64(decoded.imm[0]) : 0;
      if ( cursor.error != nullptr
           || (heap_type != -16 && heap_type != -17 && (heap_type < -22 || heap_type > -19)) )
      {
        reject(error_code::policy_gc_types,
               "Native wasmhex ref.null supports funcref, externref and eq references only");
        return false;
      }
      r.pos = cursor.pos;
      if ( heap_type != -16 && heap_type != -17 && heap_type != -20 )
        has_generic_eqref = true;
      has_externref |= heap_type == -17;
      abstract_stack.clear();
      preceding_i31_ref = heap_type == -0x14;
      continue;
    }
    if ( opcode == 0xD1 || opcode == 0xD4 )
    {
      abstract_stack.clear();
      continue;
    }
    if ( opcode == 0xD2 )
    {
      uint64 function_index;
      if ( !r.uleb(&function_index) || function_index > UINT32_MAX
           || function_index >= imported_func_types.size() + defined_func_types.size()
           || std::find(declared_function_indices.begin(), declared_function_indices.end(),
                        uint32(function_index))
             == declared_function_indices.end() )
      {
        reject(error_code::bad_instruction,
               "WASM ref.func requires a valid declared function index");
        return false;
      }
      abstract_stack.clear();
      preceding_ref_fact.known = true;
      preceding_ref_fact.targets.push_back(uint32(function_index));
      continue;
    }
    if ( opcode == 0xFB )
    {
      uint64 subopcode;
      if ( !r.uleb(&subopcode) )
        return false;
      if ( subopcode == 0x1C )
      {
        uses_i31 = true;
        preceding_i31_ref = true;
        abstract_stack.clear();
        continue;
      }
      if ( subopcode == 0x1D || subopcode == 0x1E )
      {
        uses_i31 = true;
        if ( !previous_i31_ref )
        {
          reject(
            error_code::policy_i31_constraints,
            "Native wasmhex i31.get requires an i31 reference from a supported producer or local");
          return false;
        }
        abstract_stack.clear();
        continue;
      }
      reject(
        error_code::policy_gc_types,
        "Unsupported WebAssembly GC instruction; native wasmhex supports ref.i31 and i31.get only");
      return false;
    }
    if ( opcode == 0xD3 )
    {
      abstract_stack.clear();
      continue;
    }
    if ( opcode == 0xD5 || opcode == 0xD6 )
    {
      binary::insn_cursor_t cursor{ r.data, r.size, offset, nullptr };
      binary::decoded_insn_t decoded;
      if ( !binary::next_insn(cursor, &decoded) || decoded.imm[0] >= frames.size() )
        return false;
      r.pos = cursor.pos;
      const uint64 depth = decoded.imm[0];
      const size_t frame_index = frames[frames.size() - 1 - size_t(depth)];
      const control_frame_t &target_frame = all_frames[frame_index];
      if ( opcode == 0xD6
           && (target_frame.label_types.empty()
               || (target_frame.label_types.back() != 0x70
                   && target_frame.label_types.back() != 0x6F
                   && target_frame.label_types.back() != 0x71
                   && target_frame.label_types.back() != 0x72)) )
        return false;
      body_t::control_t control;
      control.opcode = opcode;
      control.offset = offset;
      control.types = target_frame.label_types;
      control.type_heap_types = target_frame.label_heap_types;
      control.type_ref_forms = target_frame.label_ref_forms;
      const size_t branch_index = body->control.size();
      body->control.push_back(control);
      pending_branch_t branch;
      branch.control_index = branch_index;
      branch.target_frames.push_back(frame_index);
      branches.push_back(branch);
      continue;
    }
    if ( opcode == 0x11 || opcode == 0x13 )
    {
      const bool tail_call = opcode == 0x13;
      binary::insn_cursor_t cursor{ r.data, r.size, offset, nullptr };
      binary::decoded_insn_t decoded;
      if ( !binary::next_insn(cursor, &decoded) || decoded.imm[0] >= types.size()
           || decoded.imm[1] >= table_types.size() || table_types[size_t(decoded.imm[1])] != 0x70 )
        return false;
      r.pos = cursor.pos;
      const uint64 type_index = decoded.imm[0];
      const uint64 table_index = decoded.imm[1];
      const uchar table_index_type = table64[size_t(table_index)] != 0 ? uchar(0x7E) : uchar(0x7F);
      const bool direct_index_constant = !abstract_stack.empty() && abstract_stack.back().known
        && abstract_stack.back().type == table_index_type;
      const uint64 direct_index_value = direct_index_constant ? abstract_stack.back().value : 0;
      const wasm_type_t &signature = types[size_t(type_index)];
      if ( tail_call && signature.results != function_type.results )
      {
        reject(error_code::policy_call_ref_subset,
               "Native wasmhex return_call_indirect requires the caller's exact result type");
        return false;
      }
      for ( uchar type : signature.params )
        if ( type != 0x7F && type != 0x7E && type != 0x7D && type != 0x7C )
        {
          reject(error_code::policy_other,
                 "Native wasmhex call_indirect supports numeric scalar parameters only");
          return false;
        }
      size_t result_size = 0;
      if ( signature.results.size() == 1 )
      {
        const uchar type = signature.results[0];
        if ( type != 0x7F && type != 0x7E && type != 0x7D && type != 0x7C )
        {
          reject(error_code::policy_other,
                 "Native wasmhex call_indirect supports numeric scalar results only");
          return false;
        }
        result_size = type == 0x7E || type == 0x7C ? 8 : 4;
      }
      else if ( signature.results.size() > 1 )
      {
        size_t reference_count = 0;
        for ( size_t i = 0; i < signature.results.size(); ++i )
        {
          const uchar type = signature.results[i];
          const bool numeric = type == 0x7F || type == 0x7E || type == 0x7D || type == 0x7C;
          const bool reference = is_abstract_reference_handle(type, signature.result_heap_types[i]);
          if ( !numeric && (!reference || ++reference_count > 1) )
          {
            reject(
              error_code::policy_other,
              "Native wasmhex call_indirect multi-value results support numeric scalars and one abstract funcref/externref handle only");
            return false;
          }
          result_size += type == 0x7E || type == 0x7C ? 8 : 4;
        }
      }
      if ( result_size > size_t(std::numeric_limits<int>::max()) )
      {
        reject(error_code::policy_other,
               "Native wasmhex indirect-call result tuple exceeds IDA's supported object size");
        return false;
      }
      if ( !tail_call && signature.results.size() > 1 )
        body->max_tuple_call_bytes = std::max(body->max_tuple_call_bytes, result_size);
      if ( tail_call && signature.results.size() > 1 && result_size <= 16
           && (signature.results.size() != 2 || signature.results[0] != 0x7F
               || (signature.results[1] != 0x7F
                   && !is_abstract_reference_handle(signature.results[1],
                                                    signature.result_heap_types[1]))) )
      {
        reject(
          error_code::policy_call_ref_subset,
          "Native wasmhex return_call_indirect register multi-value ABI is proven only for (i32,i32); sret tuples above 16 bytes are supported");
        return false;
      }
      if ( direct_index_constant && table_dynamic_elements[size_t(table_index)] == 0
           && table_imported[size_t(table_index)] == 0 && table_exported[size_t(table_index)] == 0
           && table_shared[size_t(table_index)] == 0
           && size_t(table_index) < table_minimums.size() )
      {
        body->resolved_calls.emplace_back();
        body_t::resolved_call_t &call = body->resolved_calls.back();
        call.offset = offset;
        if ( direct_index_value > UINT32_MAX )
        {
          reject(error_code::policy_other,
                 "Native wasmhex table64 slot metadata requires a uint32 slot index");
          return false;
        }
        if ( table_imported[size_t(table_index)] == 0
             && direct_index_value >= table_minimums[size_t(table_index)] )
        {
          call.trap = wasmhex_call_trap_t::bounds;
        }
        else
        {
          const table_slot_t *slot
            = find_table_slot(uint32(table_index), uint32(direct_index_value));
          if ( slot == nullptr )
          {
            if ( table_imported[size_t(table_index)] != 0 )
            {
              reject(
                error_code::policy_other,
                "Native wasmhex imported-table call_indirect requires an active initialized slot");
              return false;
            }
            call.trap = wasmhex_call_trap_t::null_reference;
          }
          else if ( !function_types_compatible(uint32(type_index), slot->function_type_index) )
            call.trap = wasmhex_call_trap_t::type_mismatch;
          else
            call.function_index = slot->function_index;
        }
      }
      else
      {
        uint64 dynamic_table_size;
        uint32 table_size;
        if ( tail_call )
        {
          if ( !static_known_call_table(uint32(table_index), &table_size) )
          {
            reject(error_code::policy_other,
                   "Native wasmhex dynamic return_call_indirect requires a statically known table");
            return false;
          }
          if ( table_size == 0 )
          {
            body->resolved_calls.emplace_back();
            body_t::resolved_call_t &call = body->resolved_calls.back();
            call.offset = offset;
            call.trap = wasmhex_call_trap_t::bounds;
          }
          else
          {
            body->resolved_calls.emplace_back();
            body_t::resolved_call_t &call = body->resolved_calls.back();
            call.offset = offset;
            call.sparse_table_call = true;
            call.tail_dispatch = true;
            call.table_index = uint32(table_index);
            call.type_index = uint32(type_index);
            call.table_size = table_size;
            if ( !resolve_table_cases(&call, uint32(table_index), uint32(type_index), table_size) )
              return false;
          }
        }
        else if ( static_known_call_table(uint32(table_index), &table_size) )
        {
          body->resolved_calls.emplace_back();
          body_t::resolved_call_t &call = body->resolved_calls.back();
          call.offset = offset;
          call.after = uint32(r.pos);
          call.sparse_table_call = true;
          call.table_index = uint32(table_index);
          call.type_index = uint32(type_index);
          call.table_size = table_size;
          if ( !resolve_table_cases(&call, uint32(table_index), uint32(type_index), table_size) )
            return false;
        }
        else if ( static_dynamic_call_table(uint32(table_index), uint32(type_index),
                                            &dynamic_table_size) )
        {
          body->resolved_calls.emplace_back();
          body_t::resolved_call_t &call = body->resolved_calls.back();
          call.offset = offset;
          call.dynamic_table_call = true;
          call.table64_index = table64[size_t(table_index)] != 0;
          call.table_index = uint32(table_index);
          call.type_index = uint32(type_index);
          call.dynamic_table_size = dynamic_table_size;
        }
        else
        {
          reject(error_code::policy_other,
                 "Native wasmhex call_indirect requires a statically known table");
          return false;
        }
      }
      abstract_stack.clear();
      continue;
    }
    if ( opcode == 0x14 || opcode == 0x15 )
    {
      if ( !previous_ref_fact.known || previous_ref_fact.targets.empty() )
      {
        reject(error_code::policy_call_ref_subset,
               "Native wasmhex call_ref requires a statically known function-reference target");
        return false;
      }
      binary::insn_cursor_t cursor{ r.data, r.size, offset, nullptr };
      binary::decoded_insn_t decoded;
      if ( !binary::next_insn(cursor, &decoded) || decoded.imm[0] >= types.size() )
      {
        reject(error_code::policy_other,
               "Native wasmhex call_ref has an invalid function type index");
        return false;
      }
      r.pos = cursor.pos;
      const uint64 type_index = decoded.imm[0];
      const wasm_type_t &signature = types[size_t(type_index)];
      auto scalar_abi_type = [](uchar type)
      {
        return type == 0x7F || type == 0x7E || type == 0x7D || type == 0x7C || type == 0x70
          || type == 0x6F || type == 0x71;
      };
      // Mirrors the loader-side argument ABI word accounting (see
      // ida/abi.cpp); the previous argloc_t-based probe could never fail for
      // stack arguments anyway, so the pure form keeps the same outcome.
      const auto argument_on_stack = [](size_t word_index, int size)
      {
        return word_index >= 6 || (size == 8 && word_index + 1 >= 6);
      };
      size_t argument_words = 0;
      for ( uchar type : signature.params )
      {
        const int size = type == 0x7E || type == 0x7C ? 8 : 4;
        if ( !scalar_abi_type(type) )
        {
          reject(
            error_code::policy_call_ref_subset,
            "Native wasmhex call_ref requires scalar numeric/reference-handle parameters within the supported register ABI");
          return false;
        }
        argument_words += size_t(size) / 4
          + (argument_on_stack(argument_words, size) && argument_words < 6 ? 1 : 0);
      }
      size_t result_size = 0;
      if ( signature.results.size() == 1 && !scalar_abi_type(signature.results[0]) )
      {
        reject(
          error_code::policy_call_ref_subset,
          "Native wasmhex call_ref supports only void or one scalar numeric/reference-handle result");
        return false;
      }
      if ( signature.results.size() > 1 )
      {
        size_t reference_count = 0;
        for ( size_t i = 0; i < signature.results.size(); ++i )
        {
          const uchar type = signature.results[i];
          const bool numeric = type == 0x7F || type == 0x7E || type == 0x7D || type == 0x7C;
          const bool reference = is_abstract_reference_handle(type, signature.result_heap_types[i]);
          if ( !numeric && (!reference || ++reference_count > 1) )
          {
            reject(
              error_code::policy_call_ref_subset,
              "Native wasmhex call_ref multi-value results support numeric scalars and one abstract funcref/externref handle only");
            return false;
          }
          result_size += type == 0x7E || type == 0x7C ? 8 : 4;
        }
        if ( result_size > size_t(std::numeric_limits<int>::max()) )
        {
          reject(error_code::policy_call_ref_subset,
                 "Native wasmhex call_ref result tuple exceeds IDA's supported object size");
          return false;
        }
        if ( opcode == 0x14 )
          body->max_tuple_call_bytes = std::max(body->max_tuple_call_bytes, result_size);
      }
      if ( opcode == 0x15 && signature.results != function_type.results )
      {
        reject(error_code::policy_other,
               "Native wasmhex return_call_ref result type does not match the caller");
        return false;
      }
      if ( opcode == 0x15 && signature.results.size() > 1 && result_size <= 16
           && (signature.results.size() != 2 || signature.results[0] != 0x7F
               || (signature.results[1] != 0x7F
                   && !is_abstract_reference_handle(signature.results[1],
                                                    signature.result_heap_types[1]))) )
      {
        reject(
          error_code::policy_call_ref_subset,
          "Native wasmhex return_call_ref register multi-value ABI is proven only for (i32,i32); sret tuples above 16 bytes are supported");
        return false;
      }
      body->resolved_calls.emplace_back();
      body_t::resolved_call_t &call = body->resolved_calls.back();
      call.offset = offset;
      call.after = uint32(r.pos);
      if ( previous_ref_fact.targets.size() == 1 )
      {
        call.function_index = previous_ref_fact.targets[0];
        uint32 actual_type_index;
        if ( !function_type_index(call.function_index, &actual_type_index)
             || !function_types_compatible(uint32(type_index), actual_type_index) )
        {
          reject(error_code::policy_other,
                 "Native wasmhex call_ref requires an exact-type ref.func target");
          return false;
        }
      }
      else
      {
        for ( uint32 target : previous_ref_fact.targets )
        {
          uint32 actual_type_index;
          if ( target >= UINT32_MAX || !function_type_index(target, &actual_type_index)
               || !function_types_compatible(uint32(type_index), actual_type_index) )
          {
            reject(error_code::policy_call_ref_subset,
                   "Native wasmhex finite call_ref targets must have exact function signatures");
            return false;
          }
        }
        for ( uchar type : signature.params )
          if ( type != 0x7F && type != 0x7E && type != 0x7D && type != 0x7C )
          {
            reject(
              error_code::policy_call_ref_subset,
              "Native wasmhex finite call_ref dispatch supports numeric scalar parameters only");
            return false;
          }
        for ( size_t i = 0; i < signature.results.size(); ++i )
          if ( signature.results[i] != 0x7F && signature.results[i] != 0x7E
               && signature.results[i] != 0x7D && signature.results[i] != 0x7C
               && !is_abstract_reference_handle(signature.results[i],
                                                signature.result_heap_types[i]) )
          {
            reject(
              error_code::policy_call_ref_subset,
              "Native wasmhex finite call_ref dispatch supports numeric tuples with one abstract funcref/externref handle only");
            return false;
          }
        call.reference_dispatch = true;
        call.tail_dispatch = opcode == 0x15;
        call.table_size = uint32(previous_ref_fact.targets.size());
        call.target_count = uint32(previous_ref_fact.targets.size());
        call.table_cases.resize(previous_ref_fact.targets.size());
        for ( size_t i = 0; i < previous_ref_fact.targets.size(); ++i )
        {
          // ref.func values encode function indices, not target-set ordinals.
          call.table_cases[i].slot_index = previous_ref_fact.targets[i];
          call.table_cases[i].function_index = previous_ref_fact.targets[i];
          call.table_cases[i].trap = wasmhex_call_trap_t::none;
        }
      }
      abstract_stack.clear();
      continue;
    }
    if ( opcode == 0x20 || opcode == 0x21 || opcode == 0x22 )
    {
      binary::insn_cursor_t cursor{ r.data, r.size, offset, nullptr };
      binary::decoded_insn_t decoded;
      if ( !binary::next_insn(cursor, &decoded) || decoded.imm[0] >= local_count )
        return false;
      r.pos = cursor.pos;
      const uint64 local_index = decoded.imm[0];
      const size_t index = size_t(local_index);
      const uchar local_type = index < parameter_count ? function_type.params[index]
                                                       : body->local_types[index - parameter_count];
      if ( opcode == 0x20 )
      {
        c_stack_tokens.push_back({ opcode, uint32(index) });
        if ( !frames.empty() && all_frames[frames.back()].kind == 0x06
             && !all_frames[frames.back()].in_catch )
        {
          body_t::eh_try_t &eh_try = body->eh_tries[all_frames[frames.back()].eh_try_index];
          if ( offset == eh_try.body && eh_try.payload_source_offset == UINT32_MAX
               && (local_type == 0x7F || local_type == 0x7E || local_type == 0x7D
                   || local_type == 0x7C) )
          {
            eh_try.payload_source_offset = offset;
            eh_try.payload_source_after = uint32(r.pos);
          }
        }
        constant_t value;
        // Keep the local's real value type. The old code clobbered the
        // type with 0x7F after assigning it, so every folded local constant
        // looked like i32 — an i64 constant feeding a table64 call_indirect
        // index (type 0x7E) lost its static resolution, and i64 arithmetic
        // folding matched against the wrong expected type.
        value.type = local_type;
        value.known
          = (local_type == 0x7F || local_type == 0x7E) && local_constant_known[index] != 0;
        value.value = value.known ? local_constant_values[index] : 0;
        abstract_stack.push_back(value);
        if ( local_type == 0x70 )
          preceding_ref_fact = local_ref_facts[index];
        preceding_i31_ref = local_type == 0x72;
      }
      else if ( opcode == 0x21 )
      {
        if ( local_type == 0x72 && !previous_i31_ref )
        {
          reject(error_code::policy_i31_constraints,
                 "Native wasmhex i31 local.set requires an i31 reference value");
          return false;
        }
        ++local_write_counts[index];
        c_stack_tokens.push_back({ opcode, uint32(index) });
        const constant_t value = pop_constant();
        local_constant_known[index] = (local_type == 0x7F || local_type == 0x7E) && value.known;
        local_constant_values[index] = value.known ? value.value : 0;
        local_ref_facts[index] = local_type == 0x70 ? previous_ref_fact : ref_fact_t();
      }
      else
      {
        if ( local_type == 0x72 && !previous_i31_ref )
        {
          reject(error_code::policy_i31_constraints,
                 "Native wasmhex i31 local.tee requires an i31 reference value");
          return false;
        }
        ++local_write_counts[index];
        c_stack_tokens.push_back({ opcode, uint32(index) });
        const constant_t value = abstract_stack.empty() ? constant_t() : abstract_stack.back();
        local_constant_known[index] = (local_type == 0x7F || local_type == 0x7E) && value.known;
        local_constant_values[index] = value.known ? value.value : 0;
        local_ref_facts[index] = local_type == 0x70 ? previous_ref_fact : ref_fact_t();
        if ( local_type == 0x70 )
          preceding_ref_fact = local_ref_facts[index];
        preceding_i31_ref = local_type == 0x72;
      }
      continue;
    }
    if ( opcode == 0x23 || opcode == 0x24 )
    {
      binary::insn_cursor_t cursor{ r.data, r.size, offset, nullptr };
      binary::decoded_insn_t decoded;
      if ( !binary::next_insn(cursor, &decoded) || decoded.imm[0] >= globals.size()
           || (opcode == 0x24 && globals[size_t(decoded.imm[0])].mutability == 0) )
      {
        reject(error_code::bad_instruction,
               "WASM global instruction has an invalid index or writes an immutable global");
        return false;
      }
      r.pos = cursor.pos;
      const uint64 global_index = decoded.imm[0];
      if ( uint32(global_index) == c_stack_global_index )
      {
        if ( opcode == 0x23 )
        {
          ++c_stack_get_count;
        }
        else
        {
          ++c_stack_set_count;
          const size_t n = c_stack_tokens.size();
          if ( n == 4 && c_stack_tokens[0].opcode == 0x23
               && c_stack_tokens[0].imm == c_stack_global_index && c_stack_tokens[1].opcode == 0x41
               && (c_stack_tokens[2].opcode == 0x6A || c_stack_tokens[2].opcode == 0x6B)
               && (c_stack_tokens[3].opcode == 0x21 || c_stack_tokens[3].opcode == 0x22) )
          {
            // Prologue candidate at tokens 0..4; validated at end of scan_control.
          }
          else if ( n >= 3 && c_stack_tokens[n - 3].opcode == 0x20
                    && c_stack_tokens[n - 2].opcode == 0x41
                    && (c_stack_tokens[n - 1].opcode == 0x6A
                        || c_stack_tokens[n - 1].opcode == 0x6B) )
          {
            c_stack_epilogues.push_back({
              n,
              int32(c_stack_tokens[n - 2].imm),
              c_stack_tokens[n - 1].opcode,
              c_stack_tokens[n - 3].imm,
            });
          }
          else
          {
            c_stack_unrecognized_set = true;
          }
        }
      }
      c_stack_tokens.push_back({ opcode, uint32(global_index) });
      abstract_stack.clear();
      continue;
    }
    if ( opcode == 0x1A )
    {
      c_stack_tokens.push_back({ opcode, 0 });
      (void)pop_constant();
      continue;
    }
    if ( opcode == 0x6A || opcode == 0x6B || opcode == 0x6C || opcode == 0x7C || opcode == 0x7D
         || opcode == 0x7E )
    {
      c_stack_tokens.push_back({ opcode, 0 });
      const constant_t right = pop_constant();
      const constant_t left = pop_constant();
      constant_t result;
      result.type = left.type;
      const uchar expected_type = opcode <= 0x6C ? uchar(0x7F) : uchar(0x7E);
      result.known
        = left.known && right.known && left.type == expected_type && right.type == expected_type;
      if ( result.known )
      {
        const uint64 value = opcode == 0x6A || opcode == 0x7C ? left.value + right.value
          : opcode == 0x6B || opcode == 0x7D                  ? left.value - right.value
                                                              : left.value * right.value;
        result.value = expected_type == 0x7F ? uint32(value) : value;
      }
      abstract_stack.push_back(result);
      continue;
    }
    if ( opcode == 0x26 )
    {
      reject(error_code::policy_table_mutation,
             "Unsupported WebAssembly table.set mutation; static table slot resolution is unsafe");
      return false;
    }
    if ( opcode == 0xFC )
    {
      uint64 subopcode;
      if ( !r.uleb(&subopcode) )
        return false;
      if ( subopcode <= 7 )
      {
        abstract_stack.clear();
        continue;
      }
      if ( subopcode == 8 )
      {
        uint64 data_index, memory_index;
        if ( memory_count != 1 || !has_data_count || !r.uleb(&data_index)
             || data_index >= declared_data_count || !r.uleb(&memory_index) || memory_index != 0 )
        {
          reject(error_code::bad_memory_op,
                 "Native wasmhex memory.init requires a valid data index and memory zero");
          return false;
        }
        uses_data_segment_ops = true;
        abstract_stack.clear();
        continue;
      }
      if ( subopcode == 9 )
      {
        uint64 data_index;
        if ( !has_data_count || !r.uleb(&data_index) || data_index >= declared_data_count )
        {
          reject(error_code::bad_instruction,
                 "Native wasmhex data.drop requires a valid data index");
          return false;
        }
        uses_data_segment_ops = true;
        abstract_stack.clear();
        continue;
      }
      if ( subopcode == 10 || subopcode == 11 )
      {
        uint64 destination_memory, source_memory = 0;
        if ( memory_count != 1 || !r.uleb(&destination_memory) || destination_memory != 0
             || (subopcode == 10 && (!r.uleb(&source_memory) || source_memory != 0)) )
        {
          reject(error_code::policy_multiple_memories,
                 "Native wasmhex memory.copy/fill requires the single memory at index zero");
          return false;
        }
        abstract_stack.clear();
        continue;
      }
      if ( subopcode == 16 )
      {
        uint64 table_index;
        if ( !r.uleb(&table_index) )
        {
          reject(error_code::bad_instruction,
                 "Native wasmhex table.size has a malformed table index");
          return false;
        }
        if ( table_index >= table_types.size() )
        {
          reject(error_code::bad_instruction,
                 "Native wasmhex table.size references an out-of-range table index");
          return false;
        }
        const size_t index = size_t(table_index);
        if ( index >= table_minimums.size() || index >= table_maximums.size()
             || index >= table_has_maximum.size() )
        {
          reject(error_code::bad_instruction,
                 "Native wasmhex table.size table limits are malformed");
          return false;
        }
        if ( table_has_maximum[index] == 0 || table_maximums[index] != table_minimums[index] )
        {
          reject(error_code::policy_other,
                 "Native wasmhex table.size requires declared table min equal to max");
          return false;
        }
        abstract_stack.clear();
        continue;
      }
      if ( subopcode == 12 || subopcode == 14 || subopcode == 15 || subopcode == 17 )
      {
        reject(
          error_code::policy_table_mutation,
          "Unsupported WebAssembly table mutation; table.init, table.copy, table.grow and table.fill invalidate static slots");
        return false;
      }
      if ( subopcode == 13 )
      {
        reject(error_code::policy_element_segment,
               "Unsupported WebAssembly elem.drop; passive element segments are not supported");
        return false;
      }
      return false;
    }
    if ( opcode == 0xFE )
    {
      uint64 subopcode;
      wasmhex_atomic_info_t atomic;
      if ( !r.uleb(&subopcode) || !wasmhex_atomic_info(subopcode, &atomic) )
      {
        reject(error_code::policy_other, "Unsupported or reserved native wasmhex atomic subopcode");
        return false;
      }
      if ( atomic.kind == wasmhex_atomic_kind_t::fence )
      {
        uchar reserved;
        if ( !r.byte(&reserved) || reserved != 0 )
        {
          reject(error_code::bad_atomic_op,
                 "Native wasmhex atomic.fence requires reserved immediate zero");
          return false;
        }
      }
      else
      {
        uint64 flags, memory_index = 0, offset_value;
        const unsigned expected_alignment = atomic.access_size == 8 ? 3
          : atomic.access_size == 4                                 ? 2
          : atomic.access_size == 2                                 ? 1
                                                                    : 0;
        if ( memory_count != 1 )
        {
          reject(error_code::policy_multiple_memories,
                 "Native wasmhex atomic memory operation requires one memory at index zero");
          return false;
        }
        if ( !r.uleb(&flags) || (flags & ~uint64(0x7F)) != 0 )
        {
          reject(error_code::bad_atomic_op, "Native wasmhex atomic memarg has invalid flags");
          return false;
        }
        if ( (flags & 0x3F) != expected_alignment )
        {
          reject(error_code::bad_atomic_op,
                 "Native wasmhex atomic memarg alignment must match the access width");
          return false;
        }
        if ( (flags & 0x40) != 0 && (!r.uleb(&memory_index) || memory_index != 0) )
        {
          reject(error_code::bad_atomic_op,
                 "Native wasmhex atomic memarg requires memory index zero");
          return false;
        }
        if ( !r.uleb(&offset_value, memory64 ? 64 : 32) )
        {
          reject(error_code::bad_atomic_op,
                 "Native wasmhex atomic memarg offset exceeds the selected memory address width");
          return false;
        }
      }
      abstract_stack.clear();
      continue;
    }
    if ( opcode == 0xFD )
    {
      uint64 subopcode, flags, memory_index = 0, offset_value;
      if ( !r.uleb(&subopcode) )
        return false;
      if ( subopcode != 0 && subopcode != 11 && (subopcode < 1 || subopcode > 6)
           && (subopcode < 7 || subopcode > 10) && subopcode != 92 && subopcode != 93
           && (subopcode < 84 || subopcode > 91) )
      {
        reject(
          error_code::policy_simd_subset,
          "Unsupported native wasmhex SIMD memory operation; this subset supports load/store, splat/zero and lane accesses");
        return false;
      }
      const uint64 access_width = subopcode == 0 || subopcode == 11                ? 16
        : subopcode >= 1 && subopcode <= 6                                         ? 8
        : subopcode == 7 || subopcode == 84 || subopcode == 88                     ? 1
        : subopcode == 8 || subopcode == 85 || subopcode == 89                     ? 2
        : subopcode == 9 || subopcode == 92 || subopcode == 86 || subopcode == 90  ? 4
        : subopcode == 10 || subopcode == 93 || subopcode == 87 || subopcode == 91 ? 8
                                                                                   : 0;
      if ( access_width == 0 )
        return false;
      uint64 max_alignment = 0;
      for ( uint64 width = access_width; width > 1; width >>= 1 )
        ++max_alignment;
      const bool lane_operation = subopcode >= 84 && subopcode <= 91;
      if ( memory_count != 1 || !r.uleb(&flags) || (flags & ~uint64(0x7F)) != 0
           || (flags & 0x3F) > max_alignment
           || ((flags & 0x40) != 0 && (!r.uleb(&memory_index) || memory_index != 0))
           || !r.uleb(&offset_value, memory64 ? 64 : 32) )
      {
        reject(
          error_code::policy_simd_subset,
          "Native wasmhex SIMD memory access requires one memory at index zero and a valid memarg");
        return false;
      }
      if ( lane_operation )
      {
        uchar lane;
        if ( !r.byte(&lane) || lane >= 16 / access_width )
        {
          reject(error_code::policy_simd_subset,
                 "Native wasmhex SIMD lane memory operation has an invalid lane index");
          return false;
        }
      }
      if ( subopcode == 0 || subopcode == 11 || (subopcode >= 1 && subopcode <= 6)
           || (subopcode >= 7 && subopcode <= 10) || subopcode == 92 || subopcode == 93
           || lane_operation )
      {
        body->memory_accesses.emplace_back();
        body_t::memory_access_t &access = body->memory_accesses.back();
        access.offset = offset;
        access.memarg = offset_value;
        access.width = uchar(access_width);
        access.store = subopcode == 11 || (subopcode >= 88 && subopcode <= 91);
        access.lane = lane_operation;
      }
      abstract_stack.clear();
      continue;
    }
    if ( opcode >= 0x28 && opcode <= 0x3E )
    {
      static constexpr uchar access_widths[] = {
        4, 8, 4, 8, 1, 1, 2, 2, 1, 1, 2, 2, 4, 4, 4, 8, 4, 8, 1, 2, 1, 2, 4,
      };
      static constexpr uchar max_alignments[] = {
        2, 3, 2, 3, 0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 2, 3, 2, 3, 0, 1, 0, 1, 2,
      };
      const uchar access_width = access_widths[opcode - 0x28];
      const bool store = opcode >= 0x36;
      binary::insn_cursor_t cursor{ r.data, r.size, offset, nullptr };
      binary::decoded_insn_t decoded;
      uint64 flags = 0, memory_index = 0, memarg = 0;
      if ( memory_count != 1 || !r.uleb(&flags) || (flags & ~uint64(0x7F)) != 0
           || (flags & 0x3F) > max_alignments[opcode - 0x28]
           || ((flags & 0x40) != 0 && (!r.uleb(&memory_index) || memory_index != 0))
           || !r.uleb(&memarg, memory64 ? 64 : 32) || !binary::next_insn(cursor, &decoded)
           || cursor.pos != r.pos || decoded.imm[1] != memarg )
      {
        reject(error_code::bad_memory_op,
               "Native wasmhex scalar memory access has an invalid memarg");
        return false;
      }
      const size_t known_count = abstract_stack.size();
      if ( frames.size() == 1 && offset == 0 && known_count < (store ? 2 : 1) )
      {
        reject(error_code::bad_instruction,
               "Native wasmhex scalar memory access has operand stack underflow");
        return false;
      }
      if ( memory_count == 1 )
      {
        body->memory_accesses.emplace_back();
        body_t::memory_access_t &access = body->memory_accesses.back();
        access.offset = offset;
        access.memarg = memarg;
        access.width = access_width;
        access.scalar_opcode = opcode;
        access.store = store;
        access.scalar = true;
      }
      abstract_stack.clear();
      continue;
    }
    if ( opcode == 0x41 || opcode == 0x42 || opcode == 0x43 || opcode == 0x44 )
    {
      // Advance through the shared instruction-stream walker.
      binary::insn_cursor_t cursor{ r.data, r.size, offset, nullptr };
      binary::decoded_insn_t decoded;
      if ( !binary::next_insn(cursor, &decoded) )
        return false;
      r.pos = cursor.pos;
      if ( opcode == 0x41 )
      {
        const int64 value = int64(decoded.imm[0]);
        if ( value < INT32_MIN || value > INT32_MAX )
          return false;
        c_stack_tokens.push_back({ opcode, uint32(int32(value)) });
        constant_t constant;
        constant.known = true;
        constant.type = 0x7F;
        constant.value = uint64(uint32(value));
        abstract_stack.push_back(constant);
      }
      else if ( opcode == 0x42 )
      {
        constant_t constant;
        constant.known = true;
        constant.type = 0x7E;
        constant.value = decoded.imm[0];
        abstract_stack.push_back(constant);
      }
      else
      {
        abstract_stack.clear();
      }
      continue;
    }
    if ( !skip_supported_instruction(&r, opcode, memory64, memory_count) )
      return false;
    abstract_stack.clear();
  }

  if ( !finished || function_end == UINT32_MAX )
    return false;
  for ( const pending_branch_t &branch : branches )
  {
    body_t::control_t &control = body->control[branch.control_index];
    for ( size_t frame_index : branch.target_frames )
    {
      const control_frame_t &frame = all_frames[frame_index];
      const uint32 target = frame.kind == 0x00 ? function_end
        : frame.kind == 0x03                   ? frame.body
                                               : frame.end + 1;
      control.targets.push_back(target);
    }
  }
  for ( const body_t::eh_try_t &eh_try : body->eh_tries )
  {
    if ( eh_try.throw_offset == UINT32_MAX )
      continue;
    if ( body->eh_tries.size() != 1 || body->throw_count != 1 || eh_try.call_offset != UINT32_MAX )
    {
      reject(
        error_code::policy_eh_subset,
        "Native wasmhex direct throw requires one legacy try with one throw and one matching catch");
      return false;
    }
    if ( eh_try.throw_tag_index != eh_try.tag_index )
    {
      reject(error_code::policy_other,
             "Native wasmhex direct throw tag does not match its catch tag");
      return false;
    }
  }
  if ( c_stack_global_index != UINT32_MAX && (c_stack_get_count > 0 || c_stack_set_count > 0) )
  {
    constexpr int32 max_c_frame_bytes = 1024 * 1024;
    if ( c_stack_tokens.size() < 6 || c_stack_tokens[0].opcode != 0x23
         || c_stack_tokens[0].imm != c_stack_global_index || c_stack_tokens[1].opcode != 0x41
         || (c_stack_tokens[2].opcode != 0x6A && c_stack_tokens[2].opcode != 0x6B)
         || (c_stack_tokens[3].opcode != 0x21 && c_stack_tokens[3].opcode != 0x22)
         || c_stack_tokens[4].opcode != 0x24 || c_stack_tokens[4].imm != c_stack_global_index )
    {
      body->c_frame_fallback_reason = "uses C stack pointer without a canonical entry prologue";
    }
    else
    {
      const int32 raw_delta = int32(c_stack_tokens[1].imm);
      const uchar prolog_arith = c_stack_tokens[2].opcode;
      const uint32 fp_local = c_stack_tokens[3].imm;
      const uchar fp_type = fp_local < parameter_count
        ? function_type.params[fp_local]
        : body->local_types[fp_local - parameter_count];
      const int32 frame_bytes = prolog_arith == 0x6B
        ? raw_delta
        : (raw_delta < 0 && raw_delta != INT32_MIN ? -raw_delta : raw_delta);
      const int8 direction
        = (prolog_arith == 0x6B || (prolog_arith == 0x6A && raw_delta < 0)) ? int8(-1) : int8(1);
      if ( frame_bytes <= 0 || (frame_bytes & 15) != 0 || frame_bytes > max_c_frame_bytes )
      {
        body->c_frame_fallback_reason
          = "C stack prologue size is non-positive, unaligned (<16B), or exceeds 1 MiB";
      }
      else if ( fp_local < parameter_count || fp_type != 0x7F )
      {
        body->c_frame_fallback_reason = "C stack frame pointer is not a declared i32 local";
      }
      else if ( local_write_counts[fp_local] != 1 )
      {
        body->c_frame_fallback_reason
          = "C stack frame pointer local is overwritten after the prologue";
      }
      else if ( c_stack_get_count != 1 )
      {
        body->c_frame_fallback_reason
          = "C stack pointer global is read again after the entry prologue";
      }
      else if ( c_stack_unrecognized_set )
      {
        body->c_frame_fallback_reason
          = "C stack pointer global is modified by a non-epilogue instruction (dynamic alloca/stackRestore)";
      }
      else
      {
        bool epilogues_ok = true;
        for ( const c_stack_epilogue_t &epilogue : c_stack_epilogues )
        {
          const int32 signed_restore = epilogue.arith_opcode == 0x6A
            ? epilogue.restore_delta
            : (epilogue.restore_delta != INT32_MIN ? -epilogue.restore_delta : 0);
          const int32 expected_restore = direction < 0 ? frame_bytes : -frame_bytes;
          if ( epilogue.base_local != fp_local || signed_restore != expected_restore )
          {
            epilogues_ok = false;
            break;
          }
        }
        bool return_paths_ok = epilogues_ok;
        if ( return_paths_ok )
        {
          const size_t result_count = function_type.results.size();
          auto is_epilogue_set_sp = [&](size_t token_index)
          {
            for ( const c_stack_epilogue_t &epilogue : c_stack_epilogues )
              if ( epilogue.set_sp_token_index == token_index )
                return true;
            return false;
          };
          auto check_return_site = [&](size_t exit_index)
          {
            if ( exit_index <= 4 )
              return false;
            const uchar prev_op = c_stack_tokens[exit_index - 1].opcode;
            if ( prev_op == 0x00 || prev_op == 0x08 || prev_op == 0x0C || prev_op == 0x0E
                 || prev_op == 0x0F || prev_op == 0x12 || prev_op == 0x13 || prev_op == 0x15 )
              return true;
            if ( result_count == 0 )
              return is_epilogue_set_sp(exit_index - 1);
            if ( result_count == 1 && exit_index >= 6
                 && (c_stack_tokens[exit_index - 1].opcode == 0x20
                     || c_stack_tokens[exit_index - 1].opcode == 0x41
                     || c_stack_tokens[exit_index - 1].opcode == 0x42
                     || c_stack_tokens[exit_index - 1].opcode == 0x43
                     || c_stack_tokens[exit_index - 1].opcode == 0x44)
                 && is_epilogue_set_sp(exit_index - 2) )
              return true;
            return !c_stack_epilogues.empty();
          };
          for ( size_t idx = 5; idx < c_stack_tokens.size(); ++idx )
          {
            if ( c_stack_tokens[idx].opcode == 0x0F || idx + 1 == c_stack_tokens.size() )
            {
              if ( !check_return_site(idx) )
              {
                return_paths_ok = false;
                break;
              }
            }
          }
        }
        if ( !epilogues_ok )
        {
          body->c_frame_fallback_reason
            = "C stack epilogue does not restore the exact prologue delta from the frame pointer local";
        }
        else if ( !return_paths_ok )
        {
          body->c_frame_fallback_reason
            = "C stack frame exits without restoring the stack pointer on a return path";
        }
        else
        {
          body->has_c_frame = true;
          body->c_frame.sp_global_index = c_stack_global_index;
          body->c_frame.fp_local_index = fp_local;
          body->c_frame.c_frame_bytes = uint32(frame_bytes);
          body->c_frame.direction = direction;
        }
      }
    }
  }
  return true;
}
}
