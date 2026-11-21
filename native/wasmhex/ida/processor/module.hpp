#pragma once
#include "internal.hpp"
#include "../../backend/idb/metadata_schema/metadata.hpp" // clear_wasmhex_module_index on closebase
#include "../../backend/hexrays/microcode_emitter/filter.hpp"
#include "../../backend/hexrays/ctree_presentation/ctree.hpp"
namespace wasmhex
{
class wasmhex_procmod_t;
extern wasmhex_procmod_t *active_procmod;
void clear_wasmhex_filter_for_mba(mba_t *mba);

class wasmhex_ui_listener_t final : public event_listener_t
{
  wasmhex_procmod_t *procmod;

public:
  explicit wasmhex_ui_listener_t(wasmhex_procmod_t *_procmod) : procmod(_procmod) {}
  void detach() { procmod = nullptr; }
  ssize_t idaapi on_event(ssize_t event, va_list) override;
};

class wasmhex_idb_listener_t final : public event_listener_t
{
  wasmhex_procmod_t *procmod;

public:
  explicit wasmhex_idb_listener_t(wasmhex_procmod_t *_procmod) : procmod(_procmod) {}
  void detach() { procmod = nullptr; }
  ssize_t idaapi on_event(ssize_t event, va_list) override;
};

class wasmhex_procmod_t final : public procmod_t
{
  wasmhex_filter_t *filter = nullptr;
  wasmhex_ui_listener_t ui_listener;
  wasmhex_idb_listener_t idb_listener;
  bool filter_installed = false;
  bool ctree_callback_installed = false;
  // Number of module function types, cached per database for bounds-checking
  // indexed heap types. The loader stores each type's parameter vector under
  // netnode tag 'A' at index == type index (contiguous from 0), so the count
  // is the length of that run; -1 means "not computed yet".
  mutable int module_type_count = -1;

  int get_module_type_count() const
  {
    if ( module_type_count >= 0 )
      return module_type_count;
    module_type_count = 0;
    netnode wasm_node(wasmhex_node_name, 0, false);
    if ( wasm_node != BADNODE )
      while ( module_type_count < INT_MAX
              && wasm_node.supval(uint32(module_type_count), nullptr, 0, 'A') > 0 )
        ++module_type_count;
    return module_type_count;
  }

  ssize_t decode_instruction(va_list va);
  ssize_t emulate_instruction(va_list va);
  ssize_t output_instruction(va_list va);
  ssize_t output_operand(va_list va);

public:
  // Public for the table-driven decoder's special-immediates path
  // (decode_special in processor/decode.cpp). Indexed heap types reference
  // module type indices; a value at or above the module's type count is
  // malformed and must not decode.
  bool indexed_heap_in_bounds(const wasmhex::value_type_t &value_type) const
  {
    return !value_type.has_indexed_heap()
      || uint64_t(value_type.heap_type) < uint64_t(get_module_type_count());
  }

  wasmhex_procmod_t() : ui_listener(this), idb_listener(this)
  {
    active_procmod = this;
    hook_event_listener(HT_UI, &ui_listener, nullptr);
    hook_event_listener(HT_IDB, &idb_listener, nullptr);
  }

  ~wasmhex_procmod_t() override
  {
    if ( active_procmod == this )
      active_procmod = nullptr;
    ui_listener.detach();
    idb_listener.detach();
    unhook_event_listener(HT_UI, &ui_listener);
    unhook_event_listener(HT_IDB, &idb_listener);
    if ( get_hexdsp() != nullptr )
    {
      wasmhex_float_ctree::remove();
      if ( ctree_callback_installed )
        remove_hexrays_callback(wasmhex_hexrays_callback, nullptr);
      if ( filter_installed )
        install_microcode_filter(filter, false);
      delete filter;
    }
    filter = nullptr;
  }

  void clear_for_database_close()
  {
    if ( filter != nullptr && get_hexdsp() != nullptr )
      filter->clear_for_shutdown();
    clear_wasm_function_type_cache();
    clear_wasmhex_module_index();
    clear_wasmhex_function_ir_cache();
    module_type_count = -1;
  }
  void clear_for_mba(mba_t *mba)
  {
    if ( filter != nullptr )
      filter->clear_for_mba(mba);
  }

  void restore_database_bitness()
  {
    if ( PH.id != wasmhex_proc_id )
      return;
    // IDA may reopen the IDB with its default bitness instead of the loader's setting.
    netnode wasm_node(wasmhex_node_name, 0, false);
    uchar memory_mode = 0;
    if ( wasm_node == BADNODE
         || wasm_node.supval(0, &memory_mode, sizeof(memory_mode), 'M') != sizeof(memory_mode) )
    {
      msg("wasmhex: cannot restore database bitness; module metadata is missing\n");
      return;
    }
    if ( memory_mode > 2 )
    {
      msg("wasmhex: cannot restore database bitness; invalid memory mode %u\n",
          unsigned(memory_mode));
      return;
    }
    // The SAME wire byte the loader computed ('M' memory_mode) decides the
    // database bitness — clearing the 64-bit flag while a mapped address
    // sits above 4GB makes the database unopenable, and Hex-Rays needs the
    // 8-byte pointer model (CM_N64) for those functions. Integral sizes
    // stay wasm32 (size_i/size_l = 4).
    const bool memory64 = memory_mode == 2;
    const bool wide_database = memory_mode != 0;
    inf_set_app_bitness(wide_database ? 64 : 32);
    compiler_info_t cc;
    inf_get_cc(&cc);
    if ( get_comp(cc.id) == COMP_UNK )
      cc.id = COMP_GNU;
    const cm_t ptr_cm = (wide_database ? CM_N64 : CM_N32_F48) | CM_M_NN;
    cc.cm = cm_t((cc.cm & ~(CM_MASK | CM_M_MASK)) | ptr_cm);
    cc.size_i = 4;
    cc.size_b = 1;
    cc.size_e = 4;
    cc.defalign = 0;
    cc.size_s = 2;
    cc.size_l = memory64 ? 8 : 4; // wasm32 stays 4 even on wide databases
    cc.size_ll = 8;
    set_compiler(cc, SETCOMP_OVERRIDE);
    inf_set_cc_cm(cc.cm);
    if ( til_t *idati = get_idati() )
    {
      idati->cc.cm = cc.cm;
      idati->cc.size_i = cc.size_i;
      idati->cc.size_b = cc.size_b;
      idati->cc.size_e = cc.size_e;
      idati->cc.size_s = cc.size_s;
      idati->cc.size_l = cc.size_l;
      idati->cc.size_ll = cc.size_ll;
    }
  }

  bool ensure_filter()
  {
    if ( !filter_installed )
    {
      const bool host_loaded = load_wasmhex_hexrays_host();
      const bool hexrays_ready = host_loaded && (get_hexdsp() != nullptr || init_hexrays_plugin());
      if ( !hexrays_ready )
      {
        msg("wasmhex: Hex-Rays dispatcher is unavailable; F5 filter was not installed\n");
        return false;
      }
      if ( filter == nullptr )
        filter = new wasmhex_filter_t;
      filter_installed = install_microcode_filter(filter);
      if ( !filter_installed )
      {
        msg("wasmhex: Hex-Rays rejected the WASM microcode filter\n");
        return false;
      }
    }
    if ( filter_installed && !ctree_callback_installed )
    {
      ctree_callback_installed = install_hexrays_callback(wasmhex_hexrays_callback, nullptr);
      if ( !ctree_callback_installed )
        msg("wasmhex: could not install the Hex-Rays ctree callback\n");
    }
    if ( filter_installed )
    {
      if ( !wasmhex_float_ctree::install() )
        msg("wasmhex: could not install the floating-point ctree callback\n");
    }
    return filter_installed;
  }

  void retry_filter()
  {
    if ( PH.id == wasmhex_proc_id && !filter_installed )
      ensure_filter();
  }

  ssize_t idaapi on_event(ssize_t event, va_list va) override
  {
    switch ( event )
    {
      case processor_t::ev_init: break;
      case processor_t::ev_newfile:
        module_type_count = -1;
        retry_filter();
        break;
      case processor_t::ev_oldfile:
        module_type_count = -1;
        restore_database_bitness();
        retry_filter();
        break;
      case processor_t::ev_term:
        clear_wasm_function_type_cache();
        if ( get_hexdsp() != nullptr )
        {
          if ( filter != nullptr )
            filter->clear_for_shutdown();
          wasmhex_float_ctree::remove();
          if ( ctree_callback_installed )
            remove_hexrays_callback(wasmhex_hexrays_callback, nullptr);
          if ( filter_installed )
            install_microcode_filter(filter, false);
          delete filter;
        }
        ctree_callback_installed = false;
        filter_installed = false;
        filter = nullptr;
        break;

      case processor_t::ev_ana_insn: return decode_instruction(va);

      case processor_t::ev_emu_insn: return emulate_instruction(va);

      case processor_t::ev_out_insn: return output_instruction(va);

      case processor_t::ev_out_operand: return output_operand(va);

      default: break;
    }
    return 0;
  }
};

} // namespace wasmhex
