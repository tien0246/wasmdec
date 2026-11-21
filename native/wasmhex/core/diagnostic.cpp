#include "diagnostic.hpp"

#include <cstring>

namespace wasmhex::loader
{

// Diagnostics are never derived from reason strings anymore — every
// rejection site calls module_t::reject(error_code, reason) and the code
// rides with the rejection. This stub remains for callers that only probe
// the null case; there is no string table to consult.
error_code error_code_for_reason(const char *reason)
{
  (void)reason;
  return error_code::none;
}

rejection_class_t class_for_code(error_code code)
{
  switch ( code )
  {
    case error_code::resource_local_count: return rejection_class_t::resource_limit;
    case error_code::bad_local_assignment: return rejection_class_t::spec_invalid;
    case error_code::malformed_immediate: return rejection_class_t::malformed_encoding;
    case error_code::bad_leb:
    case error_code::bad_section_size:
    case error_code::bad_magic:
    case error_code::bad_section_order: return rejection_class_t::malformed_encoding;
    case error_code::policy_gc_types:
    case error_code::policy_typed_tables:
    case error_code::policy_table_mutation:
    case error_code::policy_multiple_memories:
    case error_code::policy_memory64:
    case error_code::policy_shared_memory:
    case error_code::policy_simd_subset:
    case error_code::policy_eh_subset:
    case error_code::policy_call_ref_subset:
    case error_code::policy_i31_constraints:
    case error_code::policy_global_type:
    case error_code::policy_element_segment:
    case error_code::policy_data_segment:
    case error_code::policy_import:
    case error_code::policy_stack_limits:
    case error_code::policy_other: return rejection_class_t::policy_subset;
    case error_code::none: return rejection_class_t::accepted;
    default: return rejection_class_t::spec_invalid;
  }
}

} // namespace wasmhex::loader
