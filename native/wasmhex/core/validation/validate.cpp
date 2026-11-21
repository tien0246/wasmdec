#include "validate.hpp"

#include <cstring>

namespace wasmhex::loader
{

bool validate_module(module_t &module, const uchar *data, size_t size,
                     validation_verdict_t *verdict)
{
  if ( verdict != nullptr )
  {
    verdict->valid = true;
    verdict->status = validation_status_t::valid;
    verdict->rejection = rejection_class_t::accepted;
    verdict->reason = nullptr;
    verdict->failure_section_id = 0xFF;
  }
  module.diagnostic = diagnostic_t();
  const bool parsed = module.parse(data, size);
  if ( parsed )
    return true;

  // Every reject path already attached a structured diagnostic at the
  // source (module_t::reject). The only un-coded rejections left are
  // reader-level failures (truncated LEB / bad section size) that carry no
  // reason string at all — classify those by section shape.
  if ( module.diagnostic.code == error_code::none )
  {
    module.diagnostic.code
      = module.failure_section_id != 0xFF ? error_code::bad_section_size : error_code::bad_leb;
    module.diagnostic.cls = rejection_class_t::malformed_encoding;
  }
  module.diagnostic.section = module.failure_section_id;
  if ( module.failure_reason != nullptr )
    module.diagnostic.detail = module.failure_reason;

  if ( verdict != nullptr )
  {
    verdict->valid = false;
    verdict->reason_storage = module.failure_reason != nullptr ? module.failure_reason : "";
    verdict->reason = verdict->reason_storage.c_str();
    verdict->failure_section_id = module.failure_section_id;
    verdict->rejection = module.diagnostic.cls;
    // Map the diagnostic to the honest status vocabulary. The
    // rejection class already carries the family; status makes the
    // "cannot decide" (unsupported) outcome addressable by callers.
    switch ( module.diagnostic.cls )
    {
      case rejection_class_t::malformed_encoding:
        verdict->status = validation_status_t::malformed;
        break;
      case rejection_class_t::resource_limit:
        verdict->status = validation_status_t::resource_limit;
        break;
      case rejection_class_t::policy_subset:
      case rejection_class_t::spec_invalid:
        // An unmodeled opcode family reports spec_invalid with the
        // "Unsupported" reason prefix — surface it as the honest status.
        if ( module.diagnostic.detail.rfind("Unsupported", 0) == 0 )
          verdict->status = validation_status_t::unsupported;
        else
          verdict->status = validation_status_t::invalid;
        break;
      default: verdict->status = validation_status_t::invalid; break;
    }
  }
  return false;
}

} // namespace wasmhex::loader
