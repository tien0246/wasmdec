#include "module.hpp"

#include <name.hpp> // set_cp_validity for Go method display names

static_assert(IDA_SDK_VERSION >= 940, "wasmhex requires IDA SDK >= 9.4 (IDA_SDK_VERSION >= 940)");

namespace wasmhex
{
extern "C" __attribute__((used)) const char wasmhex_m6_s1_build_marker[]
  = "WASMHEX_BUILD_MARKER_M6_S1_SDK940_NODEPRECATED";
// Phase 2 branding: this dylib is procs/wasmdec.dylib, processor short name
// wasmdec, loader display "WebAssembly Decompiler". Verified by strings(1)
// on the deployed wasmdec.dylib.
extern "C" __attribute__((used)) const char wasmdec_phase2_brand_marker[]
  = "WASMDEC_BUILD_MARKER_PHASE2_BRAND_RENAME";

wasmhex_procmod_t *active_procmod = nullptr;

void clear_wasmhex_filter_for_mba(mba_t *mba)
{
  if ( active_procmod != nullptr )
    active_procmod->clear_for_mba(mba);
}

ssize_t idaapi wasmhex_ui_listener_t::on_event(ssize_t event, va_list)
{
  if ( procmod == nullptr || procmod != active_procmod )
    return 0;
  if ( event == ui_database_inited )
    procmod->restore_database_bitness();
  if ( event == ui_database_inited || event == ui_ready_to_run || event == ui_preprocess_action )
    procmod->retry_filter();
  return 0;
}

ssize_t idaapi wasmhex_idb_listener_t::on_event(ssize_t event, va_list)
{
  if ( procmod == nullptr || procmod != active_procmod )
    return 0;
  if ( event == idb_event::auto_empty )
    procmod->retry_filter();
  else if ( event == idb_event::closebase )
    procmod->clear_for_database_close();
  return 0;
}

ssize_t idaapi notify(void *, int event, va_list)
{
  if ( event == processor_t::ev_get_procmod )
    return size_t(new wasmhex_procmod_t);
  if ( event == processor_t::ev_init )
  {
    // Open '(' ')' '*' for NAME codepoints so the loader
    // can set Go method display names like `main.(*Ball).draw`. Measured
    // on IDA 9.5 (g9probe): set_name(SN_FORCE) accepts the dotted-paren
    // form only after set_cp_validity marks the three codepoints valid in
    // BOTH UCDR_NAME and UCDR_MANGLED (VNT_IDENT is their union). Scoped
    // to this processor's process lifetime — no other database in the
    // same idat session sees a different name alphabet.
    for ( int kind : { UCDR_NAME, UCDR_MANGLED } )
      for ( wchar32_t cp : { wchar32_t('('), wchar32_t(')'), wchar32_t('*') } )
        set_cp_validity(ucdr_kind_t(kind), cp, BADCP, true);
    return 0;
  }
  return 0;
}
} // namespace wasmhex
using namespace wasmhex;

idaman bool idaapi wasmhex_install_f5_filter()
{
  return active_procmod != nullptr && active_procmod->ensure_filter();
}

idaman void idaapi wasmhex_clear_f5_state()
{
  if ( active_procmod != nullptr )
    active_procmod->clear_for_database_close();
}

processor_t LPH = {
  IDP_INTERFACE_VERSION,
  wasmhex_proc_id,
  PR_SEGS | PR_USE32 | PR_DEFSEG32
#ifdef __EA64__
    | PR_USE64
#endif
  ,
  0,
  8,
  8,
  short_names,
  long_names,
  assemblers,
  notify,
  register_names,
  qnumber(register_names),
  wasmhex_cs_reg,
  wasmhex_ds_reg,
  0,
  wasmhex_cs_reg,
  wasmhex_ds_reg,
  nullptr,
  nullptr,
  wasmhex_null,
  wasmhex_last,
  instructions,
};
