#include "../types.hpp"

#include <algorithm>
#include <cstring>

namespace wasmhex::loader
{

bool module_t::parse_types(reader_t *r)
{
  uint64 count;
  if ( !r->vector_count(&count) )
    return false;
  types.reserve(size_t(count));
  for ( uint64 i = 0; i < count; ++i )
  {
    uchar form;
    uint64 n;
    if ( !r->byte(&form) )
      return false;
    if ( form != 0x60 )
    {
      reject(
        error_code::policy_gc_types,
        "Unsupported WebAssembly GC type form; wasmhex currently supports function types only");
      return false;
    }
    if ( !r->vector_count(&n) )
      return false;
    types.emplace_back();
    wasm_type_t &type = types.back();
    type.params.reserve(size_t(n));
    type.param_heap_types.reserve(size_t(n));
    type.param_ref_forms.reserve(size_t(n));
    for ( uint64 j = 0; j < n; ++j )
    {
      uchar vt;
      int64 heap_type;
      uchar ref_form;
      if ( !read_value_type(r, &vt, &heap_type, &ref_form) )
        return false;
      type.params.push_back(vt);
      type.param_heap_types.push_back(heap_type);
      type.param_ref_forms.push_back(ref_form);
    }
    if ( !r->vector_count(&n) )
      return false;
    type.results.reserve(size_t(n));
    type.result_heap_types.reserve(size_t(n));
    type.result_ref_forms.reserve(size_t(n));
    for ( uint64 j = 0; j < n; ++j )
    {
      uchar vt;
      int64 heap_type;
      uchar ref_form;
      if ( !read_value_type(r, &vt, &heap_type, &ref_form) )
        return false;
      type.results.push_back(vt);
      type.result_heap_types.push_back(heap_type);
      type.result_ref_forms.push_back(ref_form);
    }
  }
  if ( r->pos != r->size )
    return false;
  for ( const wasm_type_t &type : types )
    for ( int64 heap_type : type.param_heap_types )
      if ( !validate_heap_type_index(heap_type) )
        return false;
  for ( const wasm_type_t &type : types )
    for ( int64 heap_type : type.result_heap_types )
      if ( !validate_heap_type_index(heap_type) )
        return false;
  return true;
}

bool module_t::parse_tags(reader_t *r)
{
  uint64 count, type_index;
  if ( !r->vector_count(&count) || count > UINT32_MAX - tag_type_indices.size() )
    return false;
  for ( uint64 i = 0; i < count; ++i )
  {
    uchar attribute;
    if ( !r->byte(&attribute) || attribute != 0 || !r->uleb(&type_index)
         || type_index >= types.size() || !types[size_t(type_index)].results.empty() )
      return false;
    tag_type_indices.push_back(uint32(type_index));
    imported_tags.push_back(0);
  }
  return r->pos == r->size;
}

bool module_t::parse_memories(reader_t *r)
{
  uint64 count;
  if ( !r->vector_count(&count) )
    return false;
  if ( count > 1 )
  {
    reject(error_code::policy_multiple_memories,
           "Native wasmhex supports one memory at index zero; multiple memories are unsupported");
    return false;
  }
  for ( uint64 i = 0; i < count; ++i )
    if ( !parse_memory_type(r, false) )
      return false;
  return r->pos == r->size;
}

bool module_t::parse_globals(reader_t *r)
{
  uint64 count;
  if ( !r->vector_count(&count) || count > UINT32_MAX )
    return false;
  if ( count > UINT32_MAX - globals.size() )
    return false;
  globals.reserve(globals.size() + size_t(count));
  for ( uint64 i = 0; i < count; ++i )
  {
    global_t global;
    int64 heap_type = no_heap_type;
    uchar ref_form, opcode, terminator;
    if ( !read_value_type(r, &global.type, &heap_type, &ref_form) )
      return false;
    if ( ref_form != 0
         || (global.type != 0x7F && global.type != 0x7E && global.type != 0x7D
             && global.type != 0x7C) )
    {
      reject(error_code::policy_global_type,
             "Unsupported native wasmhex global type; only i32/i64/f32/f64 are implemented");
      return false;
    }
    if ( !r->byte(&global.mutability) || global.mutability > 1 || !r->byte(&opcode) )
      return false;
    int64 signed_value;
    switch ( global.type )
    {
      case 0x7F:
        if ( opcode != 0x41 || !r->sleb33(&signed_value) || signed_value < INT32_MIN
             || signed_value > INT32_MAX )
          return false;
        global.initial_bits = uint32(signed_value);
        break;
      case 0x7E:
        if ( opcode != 0x42 || !r->sleb64(&signed_value) )
          return false;
        global.initial_bits = uint64(signed_value);
        break;
      case 0x7D:
        if ( opcode != 0x43 || !r->fixed(&global.initial_bits, 4) )
          return false;
        break;
      case 0x7C:
        if ( opcode != 0x44 || !r->fixed(&global.initial_bits, 8) )
          return false;
        break;
    }
    if ( !r->byte(&terminator) || terminator != 0x0B )
      return false;
    const size_t width = global.type == 0x7E || global.type == 0x7C ? 8 : 4;
    const size_t aligned = (global_storage_size + width - 1) & ~(width - 1);
    if ( aligned > UINT32_MAX - width || aligned > max_module_size - width )
      return false;
    global.storage_offset = uint32(aligned);
    global_storage_size = aligned + width;
    globals.push_back(global);
  }
  return r->pos == r->size;
}

bool module_t::parse_data_count(reader_t *r)
{
  uint64 count;
  if ( has_data_count || !r->uleb(&count) || count > UINT32_MAX || r->pos != r->size )
    return false;
  declared_data_count = uint32(count);
  has_data_count = true;
  return true;
}

bool module_t::parse_start(reader_t *r)
{
  uint64 function_index;
  if ( !r->uleb(&function_index) )
    return false;
  if ( function_index > UINT32_MAX
       || function_index >= imported_func_types.size() + defined_func_types.size() )
    return false;
  const uint32 type_index = function_index < imported_func_types.size()
    ? imported_func_types[size_t(function_index)]
    : defined_func_types[function_index - imported_func_types.size()];
  if ( type_index >= types.size() || !types[type_index].params.empty()
       || !types[type_index].results.empty() )
    return false;
  start_function_index = uint32(function_index);
  return r->pos == r->size;
}

bool module_t::parse_data(reader_t *r)
{
  uint64 count;
  if ( !r->vector_count(&count) || count > UINT32_MAX )
    return false;
  if ( has_data_count && count != declared_data_count )
  {
    reject(error_code::bad_data_count, "WebAssembly data count does not match the data section");
    return false;
  }
  data_segment_sizes.reserve(size_t(count));
  data_segment_offsets.reserve(size_t(count));
  data_segment_data.reserve(size_t(count));
  data_segment_globals.reserve(size_t(count));
  for ( uint64 i = 0; i < count; ++i )
  {
    uint64 flags, byte_count, memory_index;
    if ( !r->uleb(&flags) )
      return false;
    if ( memory_count != 1 || memory64 )
      return false;
    if ( flags == 2 && (!r->uleb(&memory_index) || memory_index != 0) )
      return false;
    if ( flags == 0 || flags == 2 )
    {
      uchar opcode, terminator;
      if ( !r->byte(&opcode) )
        return false;
      int64 segment_offset = -1;
      uint32 segment_global = UINT32_MAX;
      if ( opcode == 0x41 )
      {
        int64 offset;
        if ( !r->sleb33(&offset) || offset < 0 || offset > INT32_MAX )
          return false;
        segment_offset = offset;
      }
      else if ( opcode == 0x23 )
      {
        uint64 global_index;
        if ( !r->uleb(&global_index) || global_index >= globals.size()
             || globals[size_t(global_index)].type != 0x7F
             || globals[size_t(global_index)].mutability != 0 )
          return false;
        segment_global = uint32(global_index);
      }
      else
      {
        reject(error_code::policy_data_segment,
               "Unsupported active data segment offset; i32.const or global.get is required");
        return false;
      }
      if ( !r->byte(&terminator) || terminator != 0x0B )
        return false;
      data_segment_offsets.push_back(segment_offset);
      data_segment_globals.push_back(segment_global);
    }
    else if ( flags != 1 )
    {
      reject(error_code::policy_data_segment,
             "Native wasmhex supports passive and memory-zero active data segments");
      return false;
    }
    else
    {
      data_segment_offsets.push_back(-1);
      data_segment_globals.push_back(UINT32_MAX);
    }
    if ( !r->uleb(&byte_count) || byte_count > r->size - r->pos )
      return false;
    data_segment_sizes.push_back(byte_count);
    bytevec payload;
    if ( byte_count != 0 )
      payload.assign(r->data + r->pos, r->data + r->pos + byte_count);
    r->pos += size_t(byte_count);
    data_segment_data.push_back(std::move(payload));
  }
  // The pclntab recovery used to run here (tail of parse_data), but
  // wasmhex_binary must not reference debuginfo symbols: the call moved to
  // the parse driver (parse_module.cpp), which is allowed to call it.
  return r->pos == r->size;
}

bool module_t::parse_tables(reader_t *r)
{
  uint64 count;
  if ( !r->vector_count(&count) )
    return false;
  for ( uint64 i = 0; i < count; ++i )
  {
    uchar table_type;
    int64 heap_type = no_heap_type;
    uchar ref_form;
    uint64 minimum, maximum, flags;
    bool has_maximum;
    if ( !read_value_type(r, &table_type, &heap_type, &ref_form)
         || !validate_heap_type_index(heap_type)
         || !skip_limits(r, &minimum, &flags, &maximum, &has_maximum) )
      return false;
    if ( table_type == 0x72 )
    {
      reject(error_code::policy_i31_constraints,
             "Native wasmhex does not store i31 references in tables");
      return false;
    }
    if ( ref_form != 0 )
    {
      reject(
        error_code::policy_typed_tables,
        "Unsupported typed WebAssembly table reference; wasmhex table metadata supports abstract references only");
      return false;
    }
    table_types.push_back(table_type);
    table_imported.push_back(0);
    table_exported.push_back(0);
    table_shared.push_back((flags & 0x02) != 0);
    table64.push_back((flags & 0x04) != 0);
    table_dynamic_elements.push_back(0);
    table_minimums.push_back(minimum);
    table_maximums.push_back(maximum);
    table_has_maximum.push_back(has_maximum ? 1 : 0);
  }
  return r->pos == r->size;
}

bool module_t::parse_imports(reader_t *r)
{
  uint64 count;
  if ( !r->vector_count(&count) )
    return false;
  for ( uint64 i = 0; i < count; ++i )
  {
    std::string module_name, field_name;
    uchar kind, attribute;
    uint64 value;
    if ( !r->name(&module_name) || !r->name(&field_name) || !r->byte(&kind) )
      return false;
    // Recognize Go modules from the import section (GOOS=js/wasip1
    // import their runtime from the "gojs"/"go" module).
    if ( !is_go && (module_name == "gojs" || module_name == "go") )
    {
      is_go = true;
      // Layer 1: the import namespace splits the eras ("gojs" arrived
      // with go1.21; "go" is the go1.19/1.20 spelling).
      if ( go_profile.import_namespace.empty() )
        go_profile.import_namespace = module_name;
    }
    switch ( kind )
    {
      case 0:
        if ( !r->uleb(&value) || value > UINT32_MAX || value >= types.size() )
          return false;
        imported_func_types.push_back(uint32(value));
        {
          function_imports.emplace_back();
          function_import_t &function = function_imports.back();
          function.module = module_name;
          function.name = field_name;
        }
        break;
      case 1:
      {
        uchar table_type;
        int64 heap_type = no_heap_type;
        uchar ref_form;
        uint64 minimum, maximum, flags;
        bool has_maximum;
        if ( !read_value_type(r, &table_type, &heap_type, &ref_form)
             || !validate_heap_type_index(heap_type)
             || !skip_limits(r, &minimum, &flags, &maximum, &has_maximum) )
          return false;
        if ( table_type == 0x72 )
        {
          reject(error_code::policy_i31_constraints,
                 "Native wasmhex does not store i31 references in imported tables");
          return false;
        }
        if ( ref_form != 0 )
        {
          reject(
            error_code::policy_typed_tables,
            "Unsupported typed WebAssembly table import; wasmhex table metadata supports abstract references only");
          return false;
        }
        table_types.push_back(table_type);
        table_imported.push_back(1);
        table_exported.push_back(0);
        table_shared.push_back((flags & 0x02) != 0);
        table64.push_back((flags & 0x04) != 0);
        table_dynamic_elements.push_back(0);
        table_minimums.push_back(minimum);
        table_maximums.push_back(maximum);
        table_has_maximum.push_back(has_maximum ? 1 : 0);
      }
      break;
      case 2:
        if ( !parse_memory_type(r, true) )
          return false;
        break;
      case 3:
      {
        global_t global;
        int64 heap_type = no_heap_type;
        uchar ref_form;
        if ( globals.size() >= UINT32_MAX
             || !read_value_type(r, &global.type, &heap_type, &ref_form)
             || !r->byte(&global.mutability) || global.mutability > 1 )
          return false;
        if ( ref_form != 0
             || (global.type != 0x7F && global.type != 0x7E && global.type != 0x7D
                 && global.type != 0x7C) )
        {
          reject(error_code::policy_global_type,
                 "Unsupported native wasmhex global import; only i32/i64/f32/f64 are implemented");
          return false;
        }
        const size_t width = global.type == 0x7E || global.type == 0x7C ? 8 : 4;
        const size_t aligned = (global_storage_size + width - 1) & ~(width - 1);
        if ( aligned > UINT32_MAX - width || aligned > max_module_size - width )
          return false;
        global.storage_offset = uint32(aligned);
        global_storage_size = aligned + width;
        global.imported = true;
        global.module_name = module_name;
        global.field_name = field_name;
        global.import_helper
          = imported_global_helper_name(uint32(globals.size()), module_name, field_name);
        globals.push_back(global);
      }
      break;
      case 4:
        if ( !r->byte(&attribute) || attribute != 0 || !r->uleb(&value) || value > UINT32_MAX
             || value >= types.size() || !types[size_t(value)].results.empty() )
          return false;
        tag_type_indices.push_back(uint32(value));
        imported_tags.push_back(1);
        break;
      default: return false;
    }
  }
  return r->pos == r->size;
}

bool module_t::parse_functions(reader_t *r)
{
  uint64 count, type_index;
  if ( !r->vector_count(&count) )
    return false;
  defined_func_types.reserve(size_t(count));
  for ( uint64 i = 0; i < count; ++i )
  {
    if ( !r->uleb(&type_index) || type_index > UINT32_MAX || type_index >= types.size() )
      return false;
    defined_func_types.push_back(uint32(type_index));
  }
  return r->pos == r->size;
}

bool module_t::parse_exports(reader_t *r)
{
  uint64 count;
  if ( !r->vector_count(&count) )
    return false;
  std::unordered_set<std::string> names;
  for ( uint64 i = 0; i < count; ++i )
  {
    std::string name;
    uchar kind;
    uint64 index;
    if ( !r->name(&name) || !r->byte(&kind) || !r->uleb(&index) || index > UINT32_MAX )
      return false;
    if ( !names.insert(std::string(name.c_str())).second )
    {
      reject(error_code::bad_export, "WebAssembly export names must be unique");
      return false;
    }
    if ( kind == 0 )
    {
      if ( index >= imported_func_types.size() + defined_func_types.size() )
      {
        reject(error_code::bad_export, "WebAssembly export references an invalid function index");
        return false;
      }
      function_exports.emplace_back();
      export_t &exp = function_exports.back();
      exp.name = name;
      exp.index = uint32(index);
      declared_function_indices.push_back(uint32(index));
    }
    else if ( kind == 1 )
    {
      if ( index >= table_exported.size() )
      {
        reject(error_code::bad_export, "WebAssembly export references an invalid table index");
        return false;
      }
      table_exported[size_t(index)] = 1;
    }
    else if ( kind == 2 )
    {
      if ( index >= memory_count )
      {
        reject(error_code::bad_export, "WebAssembly export references an invalid memory index");
        return false;
      }
      memory_exported = true;
    }
    else if ( kind == 3 )
    {
      if ( index >= globals.size() )
      {
        reject(error_code::bad_export, "WebAssembly export references an invalid global index");
        return false;
      }
      global_exports.emplace_back();
      export_t &exp = global_exports.back();
      exp.name = name;
      exp.index = uint32(index);
    }
    else if ( kind == 4 )
    {
      if ( index >= tag_type_indices.size() )
      {
        reject(error_code::bad_export, "WebAssembly export references an invalid tag index");
        return false;
      }
    }
    else
    {
      reject(error_code::bad_export, "WebAssembly export has an invalid kind");
      return false;
    }
  }
  return r->pos == r->size;
}

bool module_t::parse_elements(reader_t *r)
{
  uint64 count;
  if ( !r->vector_count(&count) )
    return false;
  for ( uint64 segment_order = 0; segment_order < count; ++segment_order )
  {
    uint64 flags, table_index = 0;
    if ( !r->uleb(&flags) )
      return false;
    if ( flags == 3 )
    {
      uchar element_kind;
      uint64 function_count;
      if ( !r->byte(&element_kind) || element_kind != 0 || !r->vector_count(&function_count) )
      {
        reject(error_code::policy_element_segment,
               "Unsupported declarative WebAssembly element segment");
        return false;
      }
      for ( uint64 i = 0; i < function_count; ++i )
      {
        uint64 function_index;
        uint32 function_type;
        if ( !r->uleb(&function_index) || function_index > UINT32_MAX
             || !function_type_index(function_index, &function_type) )
          return false;
        declared_function_indices.push_back(uint32(function_index));
      }
      continue;
    }
    if ( flags != 0 && flags != 2 )
    {
      reject(
        error_code::policy_element_segment,
        "Unsupported WebAssembly element segment; only active function-index segments are supported");
      return false;
    }
    if ( flags == 2 && !r->uleb(&table_index) )
      return false;
    if ( table_index >= table_types.size() )
    {
      reject(error_code::bad_element,
             "Active WebAssembly element segment references an invalid table index");
      return false;
    }
    const bool address64 = table64[size_t(table_index)] != 0;
    const uchar const_opcode = address64 ? uchar(0x42) : uchar(0x41);
    const uchar global_type = address64 ? uchar(0x7E) : uchar(0x7F);
    const uchar add_opcode = address64 ? uchar(0x7C) : uchar(0x6A);
    const uchar sub_opcode = address64 ? uchar(0x7D) : uchar(0x6B);
    const uchar mul_opcode = address64 ? uchar(0x7E) : uchar(0x6C);
    if ( table_types[size_t(table_index)] != 0x70 )
    {
      reject(error_code::policy_element_segment,
             "Unsupported active element segment; table32 funcref is required");
      return false;
    }
    std::vector<element_offset_value_t> stack;
    element_offset_value_t result;
    bool offset_done = false;
    uchar opcode;
    while ( !offset_done && r->byte(&opcode) )
    {
      if ( opcode == 0x0B )
      {
        if ( stack.size() != 1 )
        {
          reject(error_code::policy_element_segment,
                 "Unsupported active element offset; i32 extended-const expression is invalid");
          return false;
        }
        result = stack.back();
        offset_done = true;
        break;
      }
      if ( opcode == const_opcode )
      {
        int64 value;
        if ( (address64 ? !r->sleb64(&value) : !r->sleb33(&value))
             || (!address64 && (value < INT32_MIN || value > INT32_MAX))
             || (address64 && (value < 0 || uint64_t(value) > UINT32_MAX)) )
        {
          reject(error_code::policy_element_segment,
                 "Unsupported active element offset; i32.const is out of range");
          return false;
        }
        stack.push_back(
          element_offset_value_t{ address64 ? uint64_t(value) : uint64_t(uint32_t(value)), false });
      }
      else if ( opcode == 0x23 )
      {
        uint64 global_index;
        if ( !r->uleb(&global_index) || global_index >= globals.size()
             || !globals[size_t(global_index)].imported
             || globals[size_t(global_index)].mutability != 0
             || globals[size_t(global_index)].type != global_type )
        {
          reject(
            error_code::policy_element_segment,
            "Unsupported active element offset; immutable imported i32 global.get is required");
          return false;
        }
        stack.push_back(element_offset_value_t{ 0, true });
      }
      else if ( opcode == add_opcode || opcode == sub_opcode || opcode == mul_opcode )
      {
        if ( stack.size() < 2 )
        {
          reject(error_code::policy_element_segment,
                 "Unsupported active element offset; i32 extended-const expression is invalid");
          return false;
        }
        const element_offset_value_t right = stack.back();
        stack.pop_back();
        element_offset_value_t &left = stack.back();
        left.value = opcode == add_opcode ? left.value + right.value
          : opcode == sub_opcode          ? left.value - right.value
                                          : left.value * right.value;
        left.dynamic = left.dynamic || right.dynamic;
      }
      else
      {
        reject(
          error_code::policy_element_segment,
          "Unsupported active element offset; i32.const, global.get, i32.add, i32.sub, or i32.mul is required");
        return false;
      }
    }
    if ( !offset_done )
    {
      reject(error_code::policy_element_segment,
             "Unsupported active element offset; i32 extended-const expression is truncated");
      return false;
    }
    const bool dynamic_offset = result.dynamic;
    const uint64_t table_offset = result.value;
    if ( flags == 2 )
    {
      uchar element_kind;
      if ( !r->byte(&element_kind) )
        return false;
      if ( element_kind != 0 )
      {
        reject(error_code::policy_element_segment,
               "Unsupported active element kind; function-index segments must use funcref");
        return false;
      }
    }

    uint64 function_count;
    if ( !r->vector_count(&function_count) )
      return false;
    const uint64 table_limit
      = table_imported[size_t(table_index)] != 0 ? UINT32_MAX : table_minimums[size_t(table_index)];
    if ( dynamic_offset )
      table_dynamic_elements[size_t(table_index)] = 1;
    else if ( table_offset > table_limit || function_count > table_limit - table_offset )
    {
      reject(error_code::bad_element,
             "Active WebAssembly element segment exceeds the table's initial size");
      return false;
    }
    for ( uint64 i = 0; i < function_count; ++i )
    {
      uint64 function_index;
      uint32 function_type;
      if ( !r->uleb(&function_index) || function_index > UINT32_MAX
           || !function_type_index(function_index, &function_type) )
        return false;
      if ( !dynamic_offset )
      {
        table_slots.emplace_back();
        table_slot_t &slot = table_slots.back();
        slot.table_index = uint32(table_index);
        slot.slot_index = uint32(table_offset + i);
        slot.function_index = uint32(function_index);
        slot.function_type_index = function_type;
        slot.segment_order = uint32(segment_order);
      }
      elem_entry_t elem_entry;
      elem_entry.slot_index = uint32(table_offset + i);
      elem_entry.function_index = uint32(function_index);
      elem_entries.push_back(elem_entry);
      declared_function_indices.push_back(uint32(function_index));
    }
  }
  return r->pos == r->size && resolve_element_slots();
}
} // namespace wasmhex::loader
