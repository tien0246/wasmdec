#include <algorithm>
#include "../types.hpp"
namespace wasmhex::loader
{
bool module_t::resolve_element_slots()
{
  std::sort(table_slots.begin(), table_slots.end(),
            [](const table_slot_t &left, const table_slot_t &right)
            {
              if ( left.table_index != right.table_index )
                return left.table_index < right.table_index;
              if ( left.slot_index != right.slot_index )
                return left.slot_index < right.slot_index;
              return left.segment_order < right.segment_order;
            });
  std::vector<table_slot_t> final_slots;
  for ( size_t i = 0; i < table_slots.size(); )
  {
    size_t next = i + 1;
    while ( next < table_slots.size() && table_slots[next].table_index == table_slots[i].table_index
            && table_slots[next].slot_index == table_slots[i].slot_index )
      ++next;
    final_slots.push_back(table_slots[next - 1]);
    i = next;
  }
  table_slots = final_slots;
  return true;
}

bool module_t::function_types_compatible(uint32 expected_type, uint32 actual_type) const
{
  if ( expected_type >= types.size() || actual_type >= types.size() )
    return false;
  const wasm_type_t &expected = types[expected_type];
  const wasm_type_t &actual = types[actual_type];
  return expected.params == actual.params && expected.results == actual.results
    && expected.param_heap_types == actual.param_heap_types
    && expected.result_heap_types == actual.result_heap_types
    && expected.param_ref_forms == actual.param_ref_forms
    && expected.result_ref_forms == actual.result_ref_forms;
}

bool module_t::static_dynamic_call_table(uint32 table_index, uint32, uint64 *table_size) const
{
  if ( table_size == nullptr || table_index >= table_types.size()
       || table_index >= table_minimums.size() || table_index >= table_maximums.size()
       || table_index >= table_has_maximum.size() || table_index >= table_imported.size()
       || table_index >= table_exported.size() || table_index >= table_shared.size()
       || table_index >= table_dynamic_elements.size() || table_types[table_index] != 0x70 )
    return false;
  // Mutated or host-visible tables use runtime lookup regardless of their initial slots.
  const uint64 extent = table_minimums[table_index];
  // A zero-sized table can grow before the first host-mutated call; use one sentinel slot.
  *table_size = extent == 0 ? uint64(1) : extent;
  return true;
}

bool module_t::static_known_call_table(uint32 table_index, uint32 *table_size) const
{
  if ( table_size == nullptr || table_index >= table_types.size()
       || table_index >= table_minimums.size() || table_index >= table_imported.size()
       || table_index >= table_exported.size() || table_index >= table_shared.size()
       || table_index >= table64.size() || table_types[table_index] != 0x70
       || table_dynamic_elements[table_index] != 0 || table_imported[table_index] != 0
       || table_exported[table_index] != 0 || table_shared[table_index] != 0
       || (table64[table_index] != 0 && table_minimums[table_index] > 4096)
       || table_minimums[table_index] > UINT32_MAX )
    return false;
  *table_size = uint32(table_minimums[table_index]);
  return true;
}
}
