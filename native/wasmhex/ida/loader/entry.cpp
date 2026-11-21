#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include "internal.hpp"
#include "../../backend/idb/metadata_schema/metadata.hpp"
#include "../../backend/idb/metadata_schema/netnode_tags.hpp"
#include "options.hpp"
#include "../../core/component/component.hpp"
namespace wasmhex::loader
{
// Wire record at a function's entry EA (tag 'U') for the applied x=f
// real-return prototype; version-gated, read mismatches fail closed.
struct go_xf_wire_t
{
  uint8 version;     // 1
  uint8 slot_count;  // proven result slot count
  uint16 reserved;   // zero
  uint32 min_offset; // lowest proven result slot (window coords)
  uint32 extent;     // one past the highest result byte
  char c_name[48];   // NUL-terminated named composite C name
};
static_assert(sizeof(go_xf_wire_t) == 60, "wire stability");

extern "C" __attribute__((used)) const char wasmhexldr_m6_s1_build_marker[]
  = "WASMHEX_BUILD_MARKER_M6_S1_SDK940_NODEPRECATED";
// Dref records moved off 'R'/'r' (startup-helper string /
// rejected-control slots) onto their own '8'/'9' tags, and the global
// target is read back with the matching supval('j'). Verified by
// strings(1) on the deployed wasmhexldr.dylib.
extern "C" __attribute__((used)) const char wasmhexldr_dref_tags_marker[]
  = "WASMHEX_BUILD_MARKER_T0_REV40_DREF_TAGS";
// C string args: the loader runs the callee-side pointer-parameter walk
// (blob '4' masks + kind-4 constant-argument drefs). Verified by strings(1)
// on the deployed wasmhexldr.dylib.
extern "C" __attribute__((used)) const char wasmhexldr_c_string_args_marker[]
  = "WASMHEX_BUILD_MARKER_C_STRING_ARGS";
// The loader now writes module-level blob 't' (v1) at
// load — the aggregate call-site display is live on pclntab-carrying Go
// modules. Verified by strings(1) on the deployed wasmhexldr.dylib.
extern "C" __attribute__((used)) const char wasmhexldr_g7_t_blob_marker[]
  = "WASMHEX_BUILD_MARKER_G7_T_BLOB_WIRED";
// Display rename: every user-facing identifier emitted by the
// loader/decompiler dropped the wasmhex_ prefix (wasm_global_N, go SP/CTXT/g
// ABI names, wasm_trap_/wasm_memory_/wasm_data_drop_/wasm_run_start helpers,
// wasm_table_lookup, wasmdec: comments). Verified by strings(1) on the
// deployed wasmhexldr.dylib.
extern "C" __attribute__((used)) const char wasmhexldr_display_rename_marker[]
  = "WASMHEX_BUILD_MARKER_T0_REV43_DISPLAY_RENAME";
// Phase 2 branding: dylib outputs are procs/wasmdec.dylib +
// loaders/wasmdecldr.dylib, the loader display name is
// "WebAssembly Decompiler" and the processor short name is wasmdec (still
// distinct from every IDA built-in). Verified by strings(1)
// on the deployed wasmdecldr.dylib.
extern "C" __attribute__((used)) const char wasmdecldr_phase2_brand_marker[]
  = "WASMDEC_BUILD_MARKER_PHASE2_BRAND_RENAME";

int idaapi accept_file(qstring *fileformatname, qstring *processor, linput_t *li, const char *)
{
  if ( wasmhexldr_m6_s1_build_marker[0] == '\0' )
    return 0;
  uchar header[8];
  if ( qlseek(li, 0) != 0 || qlread(li, header, sizeof(header)) != sizeof(header) )
    return 0;
  static const uchar magic[] = { 0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00 };
  static const uchar component_magic[] = { 0x00, 'a', 's', 'm', 0x0D, 0x00 };
  qlseek(li, 0);
  if ( memcmp(header, magic, sizeof(magic)) != 0
       && memcmp(header, component_magic, sizeof(component_magic)) != 0 )
    return 0;
  *fileformatname = "WebAssembly Decompiler";
  *processor = "wasmdec";
  return 1 | ACCEPT_FIRST;
}

wasmhex_bounds_mode_t read_wasmhex_bounds_mode()
{
  // Raw environment access lives in loader/options.cpp only; the
  // fresh-load semantics are unchanged (the environment wins on a first
  // load, the stored 'w' tag survives database reloads).
  qstring value;
  if ( !options::env_string("WASMHEX_BOUNDS", &value) || value.empty()
       || std::strcmp(value.c_str(), "hidden") == 0 )
    return wasmhex_bounds_mode_t::hidden;
  if ( std::strcmp(value.c_str(), "helper") == 0 )
    loader_failure("WASMHEX_BOUNDS=helper was removed; the bounds contract is "
                   "hidden (Phu luc A), explicit stays as a test flag");
  if ( std::strcmp(value.c_str(), "explicit") == 0 )
    return wasmhex_bounds_mode_t::explicit_bounds;
  loader_failure("Invalid WASMHEX_BOUNDS '%s' (expected hidden|explicit)", value.c_str());
}

// The raw environment text is persisted under tag 'p' right after the
// netnode exists (see load_file) so later consumers read the netnode
// instead of the environment.
qstring read_wasmhex_c_stack_global_override()
{
  qstring value;
  if ( !options::env_string("WASMHEX_C_STACK_GLOBAL", &value) )
    return qstring();
  return value;
}

// Typed receivers: forward declarations — the helpers live after
// load_file; both are used inside it.
static bool ensure_step_c_struct(const module_t &module, const std::string &display,
                                 const std::string &c_name, int depth);
static bool ensure_step_c_receiver_struct(const module_t &module, const go_sig_param_t &param,
                                          int depth);

void idaapi load_file(linput_t *li, ushort, const char *)
{
  const wasmhex_bounds_mode_t bounds_mode = read_wasmhex_bounds_mode();
  const qstring c_stack_override_raw = read_wasmhex_c_stack_global_override();
  const std::string c_stack_global_override(c_stack_override_raw.c_str());
  const int64 file_size = qlsize(li);
  if ( file_size < 8 || uint64(file_size) > max_module_size )
    loader_failure("WASM module is too large or truncated");

  std::vector<uchar> bytes;
  bytes.resize(size_t(file_size));
  if ( qlseek(li, 0) != 0 || qlread(li, bytes.data(), bytes.size()) != ssize_t(bytes.size()) )
    loader_failure("Cannot read WASM module");

  module_t module;
  module.bounds_mode = bounds_mode;
  module.c_stack_global_override = c_stack_global_override;
  const uchar *module_data = bytes.data();
  size_t module_size = bytes.size();
  static const uchar core_header[] = { 0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00 };
  static const uchar component_magic[] = { 0x00, 'a', 's', 'm', 0x0D, 0x00 };
  std::vector<embedded_core_module_t> embedded_modules;
  if ( memcmp(bytes.data(), core_header, sizeof(core_header)) != 0 )
  {
    if ( memcmp(bytes.data(), component_magic, sizeof(component_magic)) != 0 )
      loader_failure("Unsupported WASM core or component binary version");
    std::string error;
    if ( !find_embedded_core_modules(bytes.data(), bytes.size(), 0, 0, &embedded_modules, &error) )
      loader_failure("%s", error.c_str());
    if ( embedded_modules.empty() )
      loader_failure("WASM component contains no embedded core module");
    module_data = embedded_modules[0].data;
    module_size = embedded_modules[0].size;
  }
  // Single validation entry point — parse + classify in one place. The
  // rejection messages are byte-identical to the previous inline handling.
  {
    validation_verdict_t verdict;
    if ( !validate_module(module, module_data, module_size, &verdict) )
    {
      // Debt: carry the machine-readable diagnostic alongside the
      // display reason (code is append-only; class names follow
      // diagnostic.cpp's rejection_class_t).
      static const char *class_names[]
        = { "accepted", "spec_invalid", "policy_subset", "malformed_encoding", "resource_limit" };
      const unsigned cls = module.diagnostic.cls != rejection_class_t::accepted
        ? unsigned(module.diagnostic.cls)
        : unsigned(class_for_code(module.diagnostic.code));
      if ( module.diagnostic.code != error_code::none )
        msg("wasmhex: module rejected [code=%u class=%s]\n", unsigned(module.diagnostic.code),
            cls < 5 ? class_names[cls] : "unknown");
      if ( verdict.reason != nullptr )
        loader_failure("%s", verdict.reason);
      if ( verdict.failure_section_id != 0xFF )
        loader_failure("Unsupported or malformed WASM core module section %u",
                       unsigned(verdict.failure_section_id));
      loader_failure("Unsupported or malformed WASM core module");
    }
  }

  // Best-effort DWARF names (embedded sections or external_debug_info
  // files) fill the same fields the name section uses, before any naming below.
  {
    char input_file_path[QMAXPATH];
    input_file_path[0] = '\0';
    get_input_file_path(input_file_path, sizeof(input_file_path));
    dwarf_apply_names(&module, input_file_path);
  }

  // Build startup helpers before assigning final function names
  std::vector<size_t> startup_offsets(module.bodies.size(), UINT32_MAX);
  std::vector<const char *> startup_helpers(module.bodies.size(), nullptr);
  for ( size_t i = 0; i < module.bodies.size(); ++i )
  {
    const uint32 function_index = uint32(module.imported_func_types.size() + i);
    std::string raw_name;
    for ( size_t e = 0; e < module.function_exports.size(); ++e )
    {
      if ( module.function_exports[e].index == function_index )
      {
        raw_name = module.function_exports[e].name;
        break;
      }
    }
    if ( raw_name.empty() )
    {
      for ( size_t n = 0; n < module.function_names.size(); ++n )
      {
        if ( module.function_names[n].index == function_index )
        {
          raw_name = module.function_names[n].name;
          break;
        }
      }
    }
    const wasm_type_t &type = module.types[module.defined_func_types[i]];
    const char *helper = function_index == module.start_function_index ? "wasm_run_start"
      : raw_name == "__wasm_call_ctors"                                ? "wasm_call_ctors"
      : raw_name == "__wasm_apply_data_relocs"                         ? "wasm_apply_data_relocs"
      : raw_name == "_ZN5arrow7compute8internal18GetBinaryLikeCastsEv"
      ? "wasm_arrow_get_binary_like_casts"
      : nullptr;
    if ( helper == nullptr || !type.params.empty() || !type.results.empty() )
      continue;
    startup_helpers[i] = helper;
    startup_offsets[i] = module.code.size();
    module.code.push_back(0x01);
    module.code.push_back(0x0B);
  }
  // Does this module have active data segments that fit the declared
  // linear-memory extent? Only then is the linear-memory segment mapped at
  // wasmhex_memory_base (passive-data modules — duckdb — keep no segment at
  // all, byte-identical to the previous layout). Global.get-based segment
  // offsets resolve through in-module immutable i32 initializers; imported
  // bases stay unknown and are skipped.
  bool maps_linear_segment = false;
  uint64 linear_span_begin = UINT64_MAX;
  uint64 linear_span_end = 0;
  uint64 linear_memory_bytes = 0;
  if ( module.memory_count != 0 )
  {
    linear_memory_bytes = module.fixed_memory() ? module.fixed_memory_bytes
                                                : uint64(module.memory_minimum_pages) * 65536;
    for ( size_t i = 0; i < module.data_segment_sizes.size(); ++i )
    {
      int64 offset = module.data_segment_offsets[i];
      if ( offset < 0 && i < module.data_segment_globals.size()
           && module.data_segment_globals[i] != UINT32_MAX )
      {
        const uint32 global_index = module.data_segment_globals[i];
        if ( global_index < module.globals.size() && !module.globals[global_index].imported )
          offset = int64(uint32(module.globals[global_index].initial_bits));
      }
      if ( offset < 0 )
        continue; // passive or unknown base
      linear_span_begin = std::min<uint64>(linear_span_begin, uint64(offset));
      linear_span_end
        = std::max<uint64>(linear_span_end, uint64(offset) + module.data_segment_sizes[i]);
    }
    maps_linear_segment = linear_span_begin != UINT64_MAX && linear_span_end > linear_span_begin
      && linear_span_end <= linear_memory_bytes
      && linear_memory_bytes <= uint64(UINT64_MAX - wasmhex_memory_base);
#ifndef __EA64__
    // A 32-bit-EA database cannot host addresses above 4GB at all.
    maps_linear_segment = false;
#endif
  }
  const uchar memory_mode = module.memory_count == 0 ? 0 : module.memory64 ? 2 : 1;
  const bool wide_database = memory_mode != 0;
  // Unbounded-memory64 policy (review condition d): fail closed with a
  // diagnostic unless the address space above the memory is provably
  // clear. A memory64 that is neither fixed-extent nor max-declared can
  // grow without bound, so NO code base is safe (the prototype's
  // initial-size placement left this as an honest gap — closed here).
  if ( module.memory64 && !module.fixed_memory() && !module.memory_has_declared_maximum )
    loader_failure("Native wasmhex D1 layout requires a bounded Memory64 (fixed extent or "
                   "declared maximum): this module's linear memory can grow without bound, so "
                   "no code base above it is provably safe");
  const ea_t code_base = d1_code_base(module);
  // Huge fixed memory64 (extent sentinel) / saturated declared maximum:
  // no EA left above the memory domain for .text — fail closed.
  if ( code_base == BADADDR )
    loader_failure("WASM linear-memory upper bound leaves no address space for the code segment");
  // The mapped linear memory lives above 4GB, so the database is a 64-bit
  // program (LFLG_64BIT) and functions live in 64-bit-addressed segments —
  // Hex-Rays refuses 32-bit functions in a 64-bit database (MERR_ONLY64)
  // and 4-byte compiler pointers in a 64-bit function (MERR_SIZEOF).
  // Integral sizes stay wasm32: size_i/size_l = 4.
#ifdef __EA64__
  inf_set_app_bitness(wide_database ? 64 : 32);
#else
  if ( wide_database )
    loader_failure("Native wasmhex Memory64 requires a 64-bit IDA database");
  inf_set_app_bitness(32);
#endif
  if ( !set_processor_type("wasmdec", SETPROC_LOADER) )
    loader_failure("IDA wasmdec processor is unavailable");
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
  cc.size_l = module.memory64 ? 8 : 4;
  cc.size_ll = 8;
  if ( !set_compiler(cc, SETCOMP_OVERRIDE) || !inf_set_cc_cm(cc.cm) )
    loader_failure("Cannot configure WASM compiler model");
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
  const sel_t memory_selector
    = module.memory_count == 0 ? BADSEL : allocate_selector(ea_t(wasmhex_memory_base >> 4));
  if ( module.memory_count != 0 && (memory_selector == BADSEL || memory_selector > UINT16_MAX) )
    loader_failure("Cannot allocate a WASM linear-memory selector for Hex-Rays");

  netnode wasm_node("$ wasmhex", 0, true);
  // Persist the environment choices for post-load consumers.
  {
    qstring option;
    read_wasmhex_option(wasm_node, 'p', "WASMHEX_C_STACK_GLOBAL", "", &option);
  }
  wasm_node.supdel(0, 'm');
  wasm_node.supdel(0, 'n');
  wasm_node.supdel(0, 'Y');
  wasm_node.supdel(0, 'w');
  wasm_node.supdel(0, 'L');
  // Same fresh-load hygiene for the Go type layer — a
  // reload of a different module (e.g. stripped, no pclntab) must not
  // leave the previous module's blob 't' behind feeding the decompiler
  // stale ArgInfo shapes.
  wasm_node.delblob(0, go_types_blob_tag);
  if ( embedded_modules.size() > 1 )
  {
    msg("wasmhex: component has %zu core modules; decompiling module 0 only "
        "(others skipped)\n",
        embedded_modules.size());
    bytevec skipped;
    skipped.push_back(1); // blob version
    const uint32 skipped_count = uint32(embedded_modules.size() - 1);
    for ( unsigned byte = 0; byte < 4; ++byte )
      skipped.push_back(uchar(skipped_count >> (byte * 8)));
    for ( size_t m = 1; m < embedded_modules.size(); ++m )
    {
      const uint64 payload_offset = embedded_modules[m].offset;
      const uint64 payload_size = embedded_modules[m].size;
      for ( unsigned byte = 0; byte < 8; ++byte )
        skipped.push_back(uchar(payload_offset >> (byte * 8)));
      for ( unsigned byte = 0; byte < 8; ++byte )
        skipped.push_back(uchar(payload_size >> (byte * 8)));
    }
    if ( !wasm_node.supset(0, skipped.data(), skipped.size(), '2') )
      loader_failure("Cannot store the WASM component module list");
  }
  // memory_mode was computed once above (one source) and
  // is stored here verbatim; the processor re-reads this 'M' byte.
  const uchar memory_shared = module.memory_shared ? 1 : 0;
  const uchar stored_bounds_mode = uchar(int(bounds_mode));
  const uint32 data_count = uint32(module.data_segment_sizes.size());
  if ( !wasm_node.supset(0, &memory_mode, sizeof(memory_mode), 'M')
       || !wasm_node.supset(0, &memory_shared, sizeof(memory_shared), 'S')
       || !wasm_node.supset(0, &stored_bounds_mode, sizeof(stored_bounds_mode), 'w')
       || !wasm_node.supset(0, &data_count, sizeof(data_count), 'd')
       || (memory_selector != BADSEL
           && !wasm_node.supset(0, &memory_selector, sizeof(memory_selector), 'Y'))
       || (module.fixed_memory()
           && !wasm_node.supset(0, &module.fixed_memory_bytes, sizeof(module.fixed_memory_bytes),
                                'm'))
       || ((module.growable_memory32 || module.growable_memory64)
           && !wasm_node.supset(0, &module.growable_memory_pages,
                                sizeof(module.growable_memory_pages), 'n'))
       || !store_table_slots(wasm_node, module.table_slots)
       || !store_table_metadata(wasm_node, module) )
    loader_failure("Cannot store WASM memory or table metadata");
  if ( module.go_provenance )
  {
    if ( !store_go_module(wasm_node) )
      loader_failure("Cannot store the WASM Go-module flag");
  }
  // The Go type layer rides blob 't' (v1 codec,
  // module-level blob key 0) — written ONCE here at load, never per
  // function. The decompiler's collect_go_call_shapes reads it to shape
  // aggregate call-site arguments (go-arg-aggregate display); until this
  // write existed the display stayed dormant (fail-closed on the absent
  // blob). Stripped modules keep the layer empty, so nothing is written
  // and their display stays neutral by design.
  if ( module.go_types.present )
  {
    std::vector<uint32> type_words;
    encode_go_type_info_blob(module.go_types, &type_words);
    if ( type_words.empty()
         || !wasm_node.setblob(type_words.data(), type_words.size() * sizeof(type_words[0]), 0,
                               go_types_blob_tag) )
      loader_failure("Cannot store the WASM Go type layer");
  }

  const uchar declared_marker = 1;
  for ( uint32 function_index : module.declared_function_indices )
    if ( !wasm_node.supset(function_index, &declared_marker, sizeof(declared_marker), 'e') )
      loader_failure("Cannot store declared WASM function index %u", function_index);
  for ( size_t i = 0; i < module.types.size(); ++i )
    if ( !store_type_vector(wasm_node, uint32(i), 'A', module.types[i].params)
         || !store_type_vector(wasm_node, uint32(i), 'B', module.types[i].results)
         || !store_reference_metadata(wasm_node, uint32(i), 'a', module.types[i].param_heap_types,
                                      module.types[i].param_ref_forms)
         || !store_reference_metadata(wasm_node, uint32(i), 'b', module.types[i].result_heap_types,
                                      module.types[i].result_ref_forms) )
      loader_failure("Cannot store WASM type signature %u", uint32(i));
  std::unordered_set<std::string> used_function_names;
  // Per-source counters for the closing summary: [0] itab, [1]
  // known-API, [2] FuncOf callback, [3] descriptor method + named sret total.
  size_t go_named_counts[4] = { 0, 0, 0, 0 };
  size_t go_named_sret_count = 0;
  // Step C: go-frame functions upgraded to real incoming-window
  // stack parameters (fail-closed counters: shape/type disagreements and
  // declaration rejections never change the prototype).
  size_t go_step_c_params_count = 0;
  size_t go_step_c_shape_skip_count = 0;
  size_t go_step_c_parse_skip_count = 0;
  size_t go_step_c_sret_keep_count = 0;
  // Typed receivers (incomplete method signatures whose receiver
  // descriptor types the first window parameter).
  size_t go_step_c_receiver_count = 0;
  size_t go_step_c_receiver_skip_count = 0;
  // go-sig-drop-pcb: Step C declarations that omit the raw `int a1@<r0>`
  // PC_B selector (pc_b_register_proof — every local-0 read is consumed by
  // the lowering, so the emitted microcode never references r0).
  size_t go_step_c_pcb_drop_count = 0;
  // x=f single results (fail-closed counter; every x=f function also
  // carries the Step C window parameters).
  size_t go_xf_results_count = 0;
  size_t go_xf_eligible_count = 0;
  // How many sret displays keep the honest `result` name/pointee
  // versus get downgraded because a caller ArgInfo group binds the phantom
  // slot at a non-result window offset (fmt.Sprintf's variadic slice).
  size_t go_sret_slot_aligned = 0;
  size_t go_sret_slot_downgraded = 0;
  size_t go_sret_slot_downgraded_named = 0;
  for ( size_t i = 0; i < module.function_imports.size(); ++i )
  {
    const function_import_t &function = module.function_imports[i];
    const wasm_type_t &type = module.types[module.imported_func_types[i]];
    if ( !store_type_vector(wasm_node, uint32(i), 'P', type.params)
         || !store_type_vector(wasm_node, uint32(i), 'Q', type.results)
         || !store_reference_metadata(wasm_node, uint32(i), 'p', type.param_heap_types,
                                      type.param_ref_forms)
         || !store_reference_metadata(wasm_node, uint32(i), 'q', type.result_heap_types,
                                      type.result_ref_forms) )
      loader_failure("Cannot store WASM import signature for function %u", uint32(i));
    const std::string name = imported_function_name(uint32(i), function.module, function.name,
                                                    module, &used_function_names);
    qstring decl;
    bool supported = false;
    if ( type.results.size() > 1 )
    {
      tinfo_t import_type;
      supported = make_multi_prototype(type, &import_type);
    }
    else
    {
      tinfo_t import_type;
      const char *known_decl = find_standard_import_prototype(qstring(name.c_str()), type);
      if ( known_decl != nullptr )
      {
        decl = known_decl;
        supported
          = parse_decl(&import_type, nullptr, get_idati(), decl.c_str(), PT_SIL | PT_SEMICOLON);
      }
      if ( !supported )
      {
        decl.clear();
        supported = make_prototype(type, qstring(name.c_str()), &decl)
          && parse_decl(&import_type, nullptr, get_idati(), decl.c_str(), PT_SIL | PT_SEMICOLON);
      }
      if ( supported )
      {
        import_type.set_named_type(get_idati(), name.c_str(), NTF_REPLACE | NTF_TYPE);
      }
    }
    if ( !supported )
      loader_failure("Unsupported WASM function import signature %u", uint32(i));
    if ( !wasm_node.supset(i, name.c_str(), name.length() + 1, 'H')
         || (type.results.size() <= 1
             && !wasm_node.supset(i, decl.c_str(), decl.length() + 1, 'I')) )
      loader_failure("Cannot store WASM import metadata for function %u", uint32(i));
    std::string full_origin;
    full_origin = format("%s.%s", function.module.c_str(), function.name.c_str());
    wasm_node.supset(i, full_origin.c_str(), full_origin.length() + 1, 'o');
  }

  bytevec global_storage;
  if ( !build_global_storage(module, &global_storage) )
    loader_failure("Cannot build initialized WASM global storage");
  ea_t global_base = BADADDR;
  if ( !global_storage.empty() )
  {
    const uint64 mapped_extent = module.memory_count != 0
      ? (module.fixed_memory() ? module.fixed_memory_bytes
                               : uint64(module.memory_minimum_pages) * 65536)
      : uint64(no_memory_code_base);
    uint64 globals_floor = (mapped_extent + 0xFFF) & ~uint64(0xFFF);
    if ( globals_floor < 0x1000 )
      globals_floor = 0x1000;
    if ( globals_floor + global_storage.size() > 0x100000000ULL )
    {
      // Huge fixed memory64: no room below 4GiB — fall back above the
      // memory domain (code base); globals access there is known-broken
      // (MERR_ARCH) and honestly recorded in the report.
      globals_floor = uint64(d1_code_base(module));
    }
    const ea_t code_end = code_base + module.code.size();
    if ( module.memory_count == 0 )
      globals_floor = uint64((code_end + 0xFFF) & ~ea_t(0xFFF));
    global_base = ea_t(globals_floor);
    if ( global_storage.size() > UINT64_MAX - global_base )
      loader_failure("WASM global storage address is out of range");
    const ea_t global_end = global_base + global_storage.size();
    if ( global_end <= global_base
         || !add_segm(0, global_base, global_end, ".globals", "DATA",
                      ADDSEG_NOSREG | ADDSEG_QUIET) )
      loader_failure("Cannot create WASM global storage segment");
    set_segment_addressing(global_base, 1);
    mem2base(global_storage.data(), global_base, global_end, -1);
  }

  if ( maps_linear_segment )
  {
    // An unbounded fixed memory64 (extent sentinel UINT64_MAX) cannot be
    // materialized; cover exactly the initialized span there.
    const uint64 segment_bytes = module.fixed_memory() && module.fixed_memory_bytes == UINT64_MAX
      ? linear_span_end
      : linear_memory_bytes;
    const ea_t memory_base = ea_t(wasmhex_memory_base);
    const ea_t memory_end = ea_t(memory_base + segment_bytes);
    if ( add_segm(0, memory_base, memory_end, "MEMORY", "DATA", ADDSEG_NOSREG | ADDSEG_QUIET) )
    {
      set_segment_addressing(memory_base, 1);
      segment_info_t memory_segment;
      if ( get_segment_info(&memory_segment, memory_base) )
      {
        memory_segment.set_perm(SEGPERM_READ | SEGPERM_WRITE);
        set_segment_info(&memory_segment);
      }
      for ( size_t i = 0; i < module.data_segment_sizes.size(); ++i )
      {
        int64 offset = module.data_segment_offsets[i];
        if ( offset < 0 && i < module.data_segment_globals.size()
             && module.data_segment_globals[i] != UINT32_MAX )
        {
          const uint32 global_index = module.data_segment_globals[i];
          if ( global_index < module.globals.size() && !module.globals[global_index].imported )
            offset = int64(uint32(module.globals[global_index].initial_bits));
        }
        if ( offset < 0 )
          continue;
        const ea_t start = ea_t(memory_base + uint64(offset));
        const uchar *payload = module.data_segment_data[i].data();
        const size_t size = module.data_segment_data[i].size();
        if ( size != 0 )
          mem2base(payload, start, start + size, -1);
        // Names: name-section subsection 9 and DWARF data globals both
        // land in data_segment_names (fill-empty policy).
        for ( size_t n = 0; n < module.data_segment_names.size(); ++n )
          if ( module.data_segment_names[n].index == uint32(i) )
          {
            const std::string &name = module.data_segment_names[n].name;
            if ( valid_identifier(name) )
              set_name(start, sanitize_identifier(name).c_str(), SN_FORCE);
            break;
          }
      }
      // Record the initialized-data span (two u64) under 'L' so the
      // decompiler resolves constant addresses inside the span to data
      // symbols and renders BSS addresses numerically (exact semantics).
      const uint64 data_span[2] = { linear_span_begin, linear_span_end };
      if ( !wasm_node.supset(0, &data_span, sizeof(data_span), 'L') )
        loader_failure("Cannot store the WASM data span");
    }
    else
    {
      // Honest degradation: without the segment, data addresses render
      // as numbers instead of symbols. Nothing else changes.
      msg("wasmhex: cannot create the linear-memory segment at 0x%llX "
          "(%llu bytes); data addresses will render as numbers\n",
          uint64(memory_base), (unsigned long long)segment_bytes);
    }
  }

  std::unordered_map<uint32, std::string> got_global_names;
  resolve_got_globals(module, &got_global_names);
  std::unordered_set<std::string> used_global_names;
  const bool go_abi_globals = module.go_profile.profile == go_profile_t::go1_26_series
    || module.go_profile.profile == go_profile_t::go1_27_series;
  const auto go_abi_global_name = [go_abi_globals, &module](size_t index) -> const char *
  {
    static const struct
    {
      const char *name;
      uchar type;
    } abi_globals[3] = { { "go_sp", 0x7F }, { "go_ctxt", 0x7E }, { "go_g", 0x7E } };
    if ( !go_abi_globals || index > 2 )
      return nullptr;
    const global_t &global = module.globals[index];
    if ( global.imported || global.type != abi_globals[index].type )
      return nullptr;
    return abi_globals[index].name;
  };
  for ( size_t i = 0; i < module.globals.size(); ++i )
  {
    const global_t &global = module.globals[i];
    if ( global_base == BADADDR )
      loader_failure("Missing WASM global storage segment");
    const size_t width = global.type == 0x7E || global.type == 0x7C ? 8 : 4;
    const ea_t address = global_base + global.storage_offset;
    const flags64_t data_flags = global.type == 0x7D ? float_flag()
      : global.type == 0x7C                          ? double_flag()
      : global.type == 0x7E                          ? qword_flag()
                                                     : dword_flag();
    if ( !create_data(address, data_flags, width, BADNODE) )
      loader_failure("Cannot create WASM global %u", uint32(i));
    std::string name;
    if ( global.imported )
    {
      name = imported_global_name(uint32(i), global.module_name, global.field_name,
                                  &used_global_names);
    }
    else
    {
      bool found_name = false;
      for ( size_t j = 0; j < module.global_names.size(); ++j )
      {
        if ( module.global_names[j].index == uint32(i)
             && valid_identifier(module.global_names[j].name) )
        {
          name = module.global_names[j].name;
          found_name = true;
          break;
        }
      }
      if ( !found_name )
      {
        for ( size_t j = 0; j < module.global_exports.size(); ++j )
        {
          if ( module.global_exports[j].index == uint32(i)
               && valid_identifier(module.global_exports[j].name) )
          {
            name = module.global_exports[j].name;
            found_name = true;
            break;
          }
        }
      }
      if ( !found_name )
      {
        auto it = got_global_names.find(uint32(i));
        if ( it != got_global_names.end() && valid_identifier(it->second) )
        {
          name = it->second;
          found_name = true;
        }
      }
      if ( !found_name )
      {
        const char *go_abi_name = go_abi_global_name(i);
        if ( go_abi_name != nullptr )
          name = go_abi_name;
        else
          name = format("wasm_global_%zu", i);
      }
      if ( !used_global_names.insert(std::string(name.c_str())).second )
      {
        std::string suffixed;
        suffixed = format("%s_%zu", name.c_str(), i);
        used_global_names.insert(std::string(suffixed.c_str()));
        name = suffixed;
      }
    }
    set_name(address, name.c_str(), SN_FORCE | SN_NODUMMY);
    if ( !store_global_metadata(wasm_node, uint32(i), global, address) )
      loader_failure("Cannot store WASM global %u", uint32(i));
  }

  if ( !module.code.empty() )
  {
    const ea_t end = code_base + module.code.size();
    // Code clears the whole linear-memory domain by construction
    // (d1_code_base); the legacy below-4GiB bound is only meaningful for
    // no-memory modules where no collision exists either.
    if ( end <= code_base || end > UINT64_MAX - 0x1000
         || !add_segm(0, code_base, end, ".text", "CODE", ADDSEG_NOSREG | ADDSEG_QUIET) )
      loader_failure("Cannot create WASM code segment");
    // Whenever linear memory is mapped (at EA 0) the database
    // is a 64-bit program, and Hex-Rays decompiles functions only when the
    // function's segment addressing matches the database bitness
    // (MERR_ONLY64 otherwise) — exactly like the memory64 path.
    if ( !set_segment_addressing(code_base, wide_database ? 2 : 1) )
      loader_failure("Cannot set WASM code segment addressing");
    mem2base(module.code.data(), code_base, end, -1);
    for ( size_t i = 0; i < module.bodies.size(); ++i )
    {
      const body_t &body = module.bodies[i];
      const bool startup = startup_helpers[i] != nullptr;
      const size_t body_offset = startup ? startup_offsets[i] : body.offset;
      const size_t body_size = startup ? size_t(2) : body.size;
      const uint32 function_index = uint32(module.imported_func_types.size() + i);
      const ea_t start = code_base + body_offset;
      const ea_t finish = start + body_size;
      insn_t insn;
      if ( create_insn(start, &insn) == 0 )
        loader_failure("Cannot decode WASM function at 0x%llX", uint64(start));
      if ( !add_func(start, finish) )
        loader_failure("Cannot create WASM function at 0x%llX", uint64(start));
      const wasm_type_t &func_sig = module.types[module.defined_func_types[i]];
      const size_t total_locals = func_sig.params.size() + (startup ? 0 : body.local_types.size());
      const size_t c_frame_bytes
        = (!startup && body.has_c_frame) ? size_t(body.c_frame.c_frame_bytes) : 0;
      // Prototype: a Go function with proven frame evidence (blob 'x'
      // GOFR tail, already built — build_go_function_irs runs inside
      // module_t::parse) maps its Go stack frame onto the decompiler frame
      // the same way the C shadow stack does: [0, frame_size) = SP-relative
      // cells. The incoming window [SP+frame_size+8, +extent) rides the
      // args area (8-byte "saved regs" models the Go return-address slot,
      // so inargoff = frame_size + 8 exactly like the real layout).
      const bool m1d2_go_frame = !startup && c_frame_bytes == 0 && module.go_provenance
        && body.go_ir.present
        && body.go_ir.frame_valid(uint32(startup ? 0 : body.size))
        // The engine rejects frames over ~64KB (MERR_HUGESTACK at
        // frsize 65552 on the runtime.call65536 trampoline) and huge Go
        // frames have nothing to gain — they stay on the raw SP-global
        // model. fs + 16 (frame+frregs rounding) must stay under 0x10000.
        && body.go_ir.frame_size <= 0xFFF0;
      size_t go_frame_bytes = 0;
      size_t go_arg_extent = 0;
      if ( m1d2_go_frame )
      {
        go_frame_bytes = size_t(body.go_ir.frame_size);
        // Cover both the param lower bound and every proven result slot so
        // the callee-side virtual cells exist for result re-loads too.
        go_arg_extent = size_t(body.go_ir.param_window);
        std::vector<loader::go_result_slot_t> m1d2_slots;
        if ( loader::go_result_slots_sorted(body.go_ir, &m1d2_slots) )
          for ( const loader::go_result_slot_t &slot : m1d2_slots )
            go_arg_extent = std::max(go_arg_extent, size_t(slot.sp_offset) + size_t(slot.width));
        go_arg_extent = (go_arg_extent + 7u) & ~size_t(7u);
        // Keep the total decompiler frame under the engine's
        // huge-stack limit — functions whose combined frame (Go frame +
        // locals + tuple buffer) would blow it stay on the raw SP-global
        // model (the decompiler gate mirrors this cap).
        const size_t m1d2_total = wasmhex_calc_frame_size(
          total_locals, startup ? 0 : body.max_tuple_call_bytes, go_frame_bytes);
        if ( m1d2_total > 256 * 1024 )
        {
          go_frame_bytes = 0;
          go_arg_extent = 0;
        }
      }
      const size_t frame_bytes = wasmhex_calc_frame_size(
        total_locals, startup ? 0 : body.max_tuple_call_bytes, c_frame_bytes + go_frame_bytes);
      if ( frame_bytes > size_t(std::numeric_limits<sval_t>::max()) )
        loader_failure("WASM function frame size overflows at 0x%llX", uint64(start));
      func_entry_info_t fn_info;
      if ( !get_func_entry_info(&fn_info, start) )
        loader_failure("Cannot query WASM function info at 0x%llX", uint64(start));
      const bool frame_ok = fn_info.get_frame_id() == BADNODE
        ? add_frame_ea(start, sval_t(frame_bytes),
                       sval_t(m1d2_go_frame && go_frame_bytes != 0 ? 8 : 0), sval_t(go_arg_extent))
        : (fn_info.get_frsize() >= asize_t(frame_bytes)
           || set_frame_size_ea(start, asize_t(frame_bytes), fn_info.get_frregs(),
                                fn_info.get_argsize()));
      if ( options::debug("mc") && go_frame_bytes != 0 )
      {
        char fbuf[176];
        int fn = qsnprintf(fbuf, sizeof(fbuf),
                           "M1D2_FRAME ea=%a frame=%zu frregs=%d argsize=%zu gofs=%zu\n", start,
                           frame_bytes, int(m1d2_go_frame && go_frame_bytes != 0 ? 8 : 0),
                           go_arg_extent, go_frame_bytes);
        if ( fn > 0 )
          ::write(2, fbuf, size_t(fn));
      }
      if ( !frame_ok )
        loader_failure("Cannot create WASM function frame at 0x%llX", uint64(start));
      wasm_node.supdel_ea(start, 's');
      // Functions the validator stopped early on (state=unsupported
      // unmodeled opcode family) carry a 'u' record; the decompiler
      // refuses F5 there with a clear reason instead of decompiling
      // unchecked code.
      if ( !startup
           && std::find(module.unsupported_function_indices.begin(),
                        module.unsupported_function_indices.end(), function_index)
             != module.unsupported_function_indices.end() )
      {
        const uchar unsupported_record[1] = { 1 };
        if ( !wasm_node.supset_ea(start, unsupported_record, sizeof(unsupported_record), 'u') )
          loader_failure("Cannot store WASM unsupported-function mark at 0x%llX", uint64(start));
        msg("wasmhex: function %u (0x%llX) uses constructs the validator does "
            "not model — F5 will fail closed there\n",
            function_index, uint64(start));
      }
      // Per-function Go shape flags (bit0 PC_B signature, bit1 SP
      // frame prologue) — recorded only for provenance-checked modules.
      if ( !startup && module.go_provenance && i < module.go_function_shapes.size()
           && module.go_function_shapes[i] != 0
           && !wasm_node.supset_ea(start, &module.go_function_shapes[i],
                                   sizeof(module.go_function_shapes[i]), 'g') )
      {
        loader_failure("Cannot store WASM Go shape flags at 0x%llX", uint64(start));
      }
      if ( !startup && (body.go_ir.present || !body.go_ir.wb_sites.empty()) )
      {
        if ( !store_go_function_ir(wasm_node, DatabaseEA{ start }, body.go_ir) )
          loader_failure("Cannot store WASM Go function IR at 0x%llX", uint64(start));
      }
      else if ( !startup && !body.go_calls.empty() )
      {
        std::vector<uint32> blob;
        blob.reserve(4 + body.go_calls.size() * 6);
        blob.push_back(2); // version
        blob.push_back(uint32(body.go_calls.size()));
        for ( const body_t::go_call_t &go_call : body.go_calls )
        {
          blob.push_back(go_call.store_offset);
          blob.push_back(go_call.call_offset);
          blob.push_back(go_call.fn_index);
          blob.push_back(go_call.sp_restore_offset);
          blob.push_back(go_call.br_offset);
          blob.push_back(uint32(go_call.args.size()));
          for ( const body_t::go_arg_t &go_arg : go_call.args )
          {
            blob.push_back(go_arg.opcode);
            blob.push_back(go_arg.sp_offset);
            blob.push_back(go_arg.store_offset);
          }
        }
        if ( !wasm_node.setblob_ea(blob.data(), blob.size() * sizeof(blob[0]), start, 'x') )
          loader_failure("Cannot store WASM Go call protocol at 0x%llX", uint64(start));
      }
      // Persist the Emscripten EH evidence (blob 'i' v1 per
      // function EA) when the loader proved invoke sites / landing pads.
      if ( !startup && body.eh_ir.present )
      {
        std::vector<uint32> eh_blob;
        loader::encode_eh_function_ir_blob(body.eh_ir, &eh_blob);
        if ( !eh_blob.empty()
             && !wasm_node.setblob_ea(eh_blob.data(), eh_blob.size() * sizeof(eh_blob[0]), start,
                                      'i') )
        {
          loader_failure("Cannot store WASM EH evidence at 0x%llX", uint64(start));
        }
      }
      // Persist the libc++ idiom evidence (blob 'b' v1 per
      // function EA) when the loader proved SSO dtor sites.
      if ( !startup && body.libcxx_ir.present )
      {
        std::vector<uint32> libcxx_blob;
        loader::encode_libcxx_function_ir_blob(body.libcxx_ir, &libcxx_blob);
        if ( !libcxx_blob.empty()
             && !wasm_node.setblob_ea(libcxx_blob.data(),
                                      libcxx_blob.size() * sizeof(libcxx_blob[0]), start, 'b') )
        {
          loader_failure("Cannot store WASM libc++ evidence at 0x%llX", uint64(start));
        }
      }
      if ( !startup && body.c_frame_fallback_reason != nullptr )
      {
        msg("wasmhex: function %u (0x%llX) C shadow stack fallback: %s\n", function_index,
            uint64(start), body.c_frame_fallback_reason);
      }
      if ( !wasm_node.easet_idx(function_index, start, 'F')
           || (!startup && body.has_c_frame
               && !wasm_node.supset_ea(start, &body.c_frame, sizeof(body.c_frame), 's'))
           || (!startup && !module.bodies[i].local_types.empty()
               && !wasm_node.setblob_ea(module.bodies[i].local_types.data(),
                                        module.bodies[i].local_types.size(), start, 'L'))
           || !store_reference_metadata(wasm_node, start, 'l', body.local_heap_types,
                                        body.local_ref_forms) )
        loader_failure("Cannot store WASM function metadata at 0x%llX", uint64(start));

      if ( !startup )
      {
        // One record per function whose control
        // metadata was withheld — version byte first, matching the accessor
        // layer's v1 layout convention. The decompiler refuses F5 for a
        // function carrying an 'r' record instead of consuming repaired
        // metadata.
        struct rejected_control_record_t
        {
          uchar version;
          uint32 bad_controls;
          ea_t first_control;
        };
        bool function_rejected = false;
        uint32 rejected_controls = 0;
        ea_t first_rejected_control = BADADDR;
        for ( const body_t::control_t &control : module.bodies[i].control )
        {
          const ea_t control_ea = start + control.offset;
          // A control whose encoded EA — its own offset, any branch
          // target or any virtual stub — falls outside [start, finish)
          // carries corrupt scan/remap data. Store nothing for it: the
          // metadata stays absent (the decompiler already handles absence
          // as "unknown control"), the function is rejected via the 'r'
          // record and the reason is reported. No clipping, snapping or
          // clamping survives this gate.
          auto outside_range = [&](ea_t encoded)
          {
            // Code EAs legitimately exceed 4GiB now — the range
            // check against [start, finish) is the whole gate.
            return encoded < start || encoded >= finish;
          };
          ea_t bad_ea = BADADDR;
          auto note_bad = [&](ea_t encoded)
          {
            if ( bad_ea == BADADDR )
              bad_ea = encoded;
          };
          if ( outside_range(control_ea) )
            note_bad(control_ea);
          for ( uint32 target : control.targets )
            if ( outside_range(start + target) )
              note_bad(start + target);
          for ( uint32 stub : control.stubs )
            if ( outside_range(start + stub) )
              note_bad(start + stub);
          if ( bad_ea != BADADDR )
          {
            if ( !function_rejected )
            {
              function_rejected = true;
              first_rejected_control = control_ea;
            }
            ++rejected_controls;
            msg("wasmhex: control at 0x%llX references 0x%llX outside function "
                "[0x%llX,0x%llX) — control metadata skipped (fail-closed)\n",
                uint64(control_ea), uint64(bad_ea), uint64(start), uint64(finish));
            continue;
          }
          if ( control.scope_end )
          {
            const ea_t end_ea = start + control.targets[0];
            if ( !wasm_node.supset_ea(control_ea, &end_ea, sizeof(end_ea), 'X')
                 || !store_type_vector(wasm_node, control_ea, 'U', control.params)
                 || !store_control_types(wasm_node, DatabaseEA{ control_ea }, control.types.data(),
                                         control.types.size())
                 || !store_type_vector(wasm_node, control_ea, 'V', control.labels)
                 || !store_reference_metadata(wasm_node, control_ea, 'u', control.param_heap_types,
                                              control.param_ref_forms)
                 || !store_reference_metadata(wasm_node, control_ea, 't', control.type_heap_types,
                                              control.type_ref_forms)
                 || !store_reference_metadata(wasm_node, control_ea, 'v', control.label_heap_types,
                                              control.label_ref_forms) )
              loader_failure("Cannot store WASM structured-scope metadata at 0x%llX",
                             uint64(control_ea));
            continue;
          }
          if ( control.opcode == 0x0B )
          {
            const uchar function_end = 1;
            if ( !wasm_node.supset_ea(control_ea, &function_end, sizeof(function_end), 'W') )
              loader_failure("Cannot store WASM function-end metadata at 0x%llX",
                             uint64(control_ea));
          }
          if ( (control.opcode == 0x0C || control.opcode == 0x0D || control.opcode == 0x0E
                || control.opcode == 0xD5 || control.opcode == 0xD6)
               && (!store_control_types(wasm_node, DatabaseEA{ control_ea }, control.types.data(),
                                        control.types.size())
                   || !store_reference_metadata(wasm_node, control_ea, 't', control.type_heap_types,
                                                control.type_ref_forms)) )
            loader_failure("Cannot store WASM branch value types at 0x%llX", uint64(control_ea));
          if ( !control.targets.empty() )
          {
            std::vector<ea_t> targets;
            targets.reserve(control.targets.size());
            // No stub EAs exist any more (the stub machinery is
            // retired) — 'C' always carries the real branch targets.
            for ( size_t j = 0; j < control.targets.size(); ++j )
              targets.push_back(start + control.targets[j]);
            // Metadata goes through the versioned accessor layer
            // (ida/metadata.hpp) — payload identical to the v0 layout, one
            // leading version byte. The v1 prefix also takes one byte of the
            // MAXSPECSIZE (1024) supval budget, so the small 'C'/'D' path
            // holds 127 targets and everything larger uses the packed 'c'
            // blob (which has no cap).
            constexpr size_t branch_supval_budget = 1024 - 1;
            if ( control.opcode == 0x0E
                 && targets.size() > branch_supval_budget / sizeof(targets[0]) )
            {
              // KHAN fix preserved inside the accessor: the blob layout is
              // count + count*depth + count*ea with the depth list zero-
              // padded to exactly one depth per target.
              if ( !store_packed_branch_table(wasm_node, DatabaseEA{ control_ea },
                                              control.depths.data(), control.depths.size(),
                                              targets.data(), targets.size()) )
                loader_failure("Cannot store large WASM branch-table metadata at 0x%llX",
                               uint64(control_ea));
            }
            else if ( !store_branch_targets(wasm_node, DatabaseEA{ control_ea }, targets.data(),
                                            targets.size()) )
              loader_failure("Cannot store WASM branch metadata at 0x%llX", uint64(control_ea));
          }
          // The 'E' (br_table edge-stub list) and 'S' (branch edge
          // stub -> source) stores are retired — no 0xF2/0xFF stub
          // controls are materialized any more.
          // 'D' only for the small-table path: netnode supvals are capped at
          // ~1KB, so a large table would truncate the depth list and leave a
          // stale-looking 'D' contradicting the packed 'c' blob (which already
          // carries the full depth list). Same threshold as the packed 'c'
          // branch above (supval budget minus the v1 version byte).
          const bool packed_branch_table
            = control.opcode == 0x0E && control.targets.size() > (1024 - 1) / sizeof(ea_t);
          // KHAN fix ('D' completeness): the decompiler requires one depth
          // per encoded target — pad a short scan list with 0 instead of
          // storing nothing (which forced fallback depth=0 / clamping).
          if ( control.opcode == 0x0E && !packed_branch_table )
          {
            std::vector<uint32> depths(control.depths.begin(), control.depths.end());
            if ( depths.size() < control.targets.size() )
              depths.resize(control.targets.size(), 0);
            if ( !depths.empty()
                 && !store_branch_depths(wasm_node, DatabaseEA{ control_ea }, depths.data(),
                                         depths.size()) )
              loader_failure("Cannot store WASM branch-table metadata at 0x%llX",
                             uint64(control_ea));
          }
          if ( false )
            loader_failure("Cannot store WASM branch-table metadata at 0x%llX", uint64(control_ea));
          // The chain dispatcher metadata ('S' dispatcher + per-case 'C')
          // is gone — the loader only emits the jtbl virtual-stub layout now.
        }
        if ( function_rejected )
        {
          const rejected_control_record_t record = { 1, rejected_controls, first_rejected_control };
          if ( !wasm_node.supset_ea(start, &record, sizeof(record), 'r') )
            loader_failure("Cannot store WASM rejected-control metadata at 0x%llX", uint64(start));
          msg("wasmhex: function 0x%llX rejected (%u control(s) withheld) — "
              "F5 must fail closed there\n",
              uint64(start), rejected_controls);
        }
        for ( const body_t::eh_try_t &eh_try : body.eh_tries )
        {
          const ea_t try_ea = start + eh_try.offset;
          const ea_t catch_ea = start + eh_try.catch_offset;
          const ea_t payload_type = eh_try.payload_type;
          const uint32 tag_index = eh_try.tag_index;
          if ( !wasm_node.supset_ea(try_ea, &catch_ea, sizeof(catch_ea), 'H')
               || !wasm_node.supset_ea(try_ea, &payload_type, sizeof(payload_type), 'N')
               || !wasm_node.supset_ea(try_ea, &tag_index, sizeof(tag_index), 'O')
               || (eh_try.call_offset != UINT32_MAX
                   && !wasm_node.supset_ea(start + eh_try.call_offset, &catch_ea, sizeof(catch_ea),
                                           'Y'))
               || (eh_try.throw_offset != UINT32_MAX
                   && !wasm_node.supset_ea(start + eh_try.throw_offset, &catch_ea, sizeof(catch_ea),
                                           'Y')) )
            loader_failure("Cannot store WASM exception handler edge at 0x%llX",
                           uint64(start
                                  + (eh_try.throw_offset != UINT32_MAX ? eh_try.throw_offset
                                                                       : eh_try.call_offset)));
        }
        for ( const body_t::resolved_call_t &call : body.resolved_calls )
        {
          if ( call.sparse_table_call || call.reference_dispatch )
          {
            // The dispatch lives entirely in metadata at the call
            // source EA — 'J' (sizes), 'Q' (case count), 'C'
            // (continuation) and the packed 'j' case blob. No stub
            // addresses exist because no stubs are written to .text.
            const ea_t source = start + call.offset;
            const ea_t continuation = start + call.after;
            const wasmhex_sparse_dispatch_t sparse_dispatch = {
              call.table_size,
              call.target_count,
            };
            const uint32 case_count = uint32(call.table_cases.size());
            bool stored
              = wasm_node.supset_ea(source, &sparse_dispatch, sizeof(sparse_dispatch), 'J')
              && wasm_node.supset_ea(source, &case_count, sizeof(case_count), 'Q');
            if ( stored && !call.tail_dispatch )
              stored = wasm_node.supset_ea(source, &continuation, sizeof(continuation), 'C');
            if ( !stored )
              loader_failure("Cannot store WASM call dispatch at 0x%llX", uint64(source));
            // Packed case blob 'j' in the netnode BLOB space (supvals
            // are capped near 1KB; Go dispatch tables exceed that):
            // [count][{slot, function, trap}] * count behind a version
            // byte — the decompiler builds the chain from it alone.
            {
              qvector<uint32> case_blob;
              case_blob.reserve(call.table_cases.size() * 3);
              for ( const body_t::resolved_call_t::table_case_t &table_case : call.table_cases )
              {
                case_blob.push_back(table_case.slot_index);
                case_blob.push_back(table_case.function_index);
                case_blob.push_back(uint32(table_case.trap));
              }
              if ( !store_dispatch_cases(wasm_node, DatabaseEA{ source }, case_blob.begin(),
                                         case_blob.size() / 3) )
                loader_failure("Cannot store WASM call dispatch cases at 0x%llX", uint64(source));
            }
          }
          else if ( call.trap != wasmhex_call_trap_t::none )
          {
            if ( !wasm_node.supset_ea(start + call.offset, &call.trap, sizeof(call.trap), 'Z') )
              loader_failure("Cannot store statically trapping WASM indirect call at 0x%llX",
                             uint64(start + call.offset));
          }
          else if ( call.dynamic_table_call )
          {
            struct checked_table_call_t
            {
              uint32 table_index;
              uint32 type_index;
              uint64 table_size;
              uchar index_type;
            } checked_call = { call.table_index, call.type_index, call.dynamic_table_size,
                               call.table64_index ? uchar(0x7E) : uchar(0x7F) };
            if ( !wasm_node.supset_ea(start + call.offset, &checked_call, sizeof(checked_call),
                                      'K') )
              loader_failure("Cannot store statically checked WASM indirect table call at 0x%llX",
                             uint64(start + call.offset));
          }
          else
          {
            const uint32 function_index = call.function_index;
            if ( !wasm_node.supset_ea(start + call.offset, &function_index, sizeof(function_index),
                                      'G') )
              loader_failure("Cannot store statically resolved WASM indirect call at 0x%llX",
                             uint64(start + call.offset));
          }
        }
        for ( const body_t::memory_trap_edge_t &edge : body.memory_trap_edges )
        {
          const ea_t trap = start + edge.stub_offset;
          const ea_t check = start + edge.check_offset;
          if ( !module.has_memory_bounds_check()
               || !wasm_node.supset_ea(check, &trap, sizeof(trap), 'T')
               || !wasm_node.supset_ea(start + edge.access_offset, &check, sizeof(check), 'B') )
            loader_failure("Cannot store checked memory trap edge at 0x%llX", uint64(check));
        }
      }

      if ( module.producers_go && module.pclntab_function_names.empty() && !startup && i == 0
           && function_index == module.imported_func_types.size() )
        msg("wasmhex: Go module carries no pclntab — internal function names "
            "were stripped at build time\n");
      // A pclntab name (indexed by defined function, imports excluded)
      // fills functions the export and name sections never named.
      const std::string pclntab_name = i < module.pclntab_function_names.size()
        ? module.pclntab_function_names[i]
        : std::string();
      const std::string name
        = function_name(function_index, module.function_exports, module.function_names,
                        &used_function_names, pclntab_name.empty() ? nullptr : &pclntab_name);
      const wasm_type_t &type = module.types[module.defined_func_types[i]];
      go_named_sig_t go_named_sig;
      const bool go_named = !startup && module.go_types.present
        && go_resolve_named_signature(module, i, name, &go_named_sig);
      if ( go_named )
      {
        std::string comment = "go: " + go_named_sig.signature + " [" + go_named_sig.anchor + "]";
        // set_func_cmt_ea — the func_t*/set_func_cmt pair is
        // deprecated in SDK >= 9.4 (pointer-lifetime safety).
        set_func_cmt_ea(start, comment.c_str(), true);
        ++go_named_counts[go_named_sig.source == 1     ? 0
                            : go_named_sig.source == 2 ? 1
                            : go_named_sig.source == 3 ? 2
                                                       : 3];
      }
      std::string go_sret_pointee;
      std::vector<go_c_field_t> go_sret_fields;
      uint64 go_sret_size = 0;
      // Cross-check the phantom sret `result` slot against the
      // caller-side ArgInfo shape (core go_sret_result_slot_verdict — the
      // same evidence collect_go_call_shapes lowers call sites with).
      // kBoundArgument = a caller argument group binds the phantom slot at
      // a non-result offset: the named pointee is suppressed (inside
      // go_named_sret_pointee) AND the sret parameter is renamed aN so no
      // call site displays an argument window as the result cell.
      const go_sret_slot_verdict_t sret_slot_verdict = go_sret_result_slot_verdict(module, i);
      const bool sret_slot_lie = sret_slot_verdict == go_sret_slot_verdict_t::kBoundArgument;
      if ( go_named && go_named_sig.complete
           && go_named_sret_pointee(module, i, name, &go_sret_pointee, &go_sret_fields,
                                    &go_sret_size)
           && ensure_go_struct_type(go_sret_pointee.c_str(), go_sret_fields, go_sret_size) )
      {
        ++go_named_sret_count;
      }
      else
        go_sret_pointee.clear();
      wasm_type_t go_display_storage;
      const wasm_type_t *display = &type;
      std::vector<uchar> go_proven_result_types;
      if ( go_proven_results(module, i, &go_proven_result_types) )
      {
        go_display_storage = type;
        go_display_storage.results = go_proven_result_types;
        go_display_storage.result_heap_types.clear();
        go_display_storage.result_ref_forms.clear();
        display = &go_display_storage;
      }
      else if ( go_proven_void_results(module, i) )
      {
        go_display_storage = type;
        go_display_storage.results.clear();
        go_display_storage.result_heap_types.clear();
        go_display_storage.result_ref_forms.clear();
        display = &go_display_storage;
      }
      const char *startup_helper = startup_helpers[i];
      if ( startup_helper != nullptr && (type.params.empty() && type.results.empty())
           && !wasm_node.supset_ea(start, startup_helper, strlen(startup_helper) + 1, 'R') )
        loader_failure("Cannot store generated WASM startup helper at 0x%llX", uint64(start));
      if ( !store_type_vector(wasm_node, function_index, 'P', type.params)
           || !store_type_vector(wasm_node, start, '4', type.params)
           || !store_type_vector(wasm_node, function_index, 'Q', type.results)
           || !store_type_vector(wasm_node, start, '5', display->results)
           || !store_reference_metadata(wasm_node, function_index, 'p', type.param_heap_types,
                                        type.param_ref_forms)
           || !store_reference_metadata(wasm_node, start, '6', type.param_heap_types,
                                        type.param_ref_forms)
           || !store_reference_metadata(wasm_node, function_index, 'q', type.result_heap_types,
                                        type.result_ref_forms)
           || !store_reference_metadata(wasm_node, start, '7', display->result_heap_types,
                                        display->result_ref_forms) )
        loader_failure("Cannot store WASM function signature for function %u", function_index);
      const std::string ida_name = module.go_provenance ? go_method_display_name(name) : name;
      set_name(start, ida_name.c_str(), SN_FORCE | SN_NODUMMY);
      // Record the full multi-export alias list for this function_index
      // in netnode blob 'E' (v1: version byte + uleb count + uleb-prefixed
      // UTF-8 names) so Slice 2 can render `wasmhex ICF-folded with:` comments
      // for wasm-ld --icf multi-export functions.
      {
        std::vector<std::string> export_aliases;
        for ( const export_t &exp : module.function_exports )
          if ( exp.index == function_index && !exp.name.empty() )
            export_aliases.push_back(exp.name);
        if ( export_aliases.size() > 1 )
        {
          bytevec alias_blob;
          alias_blob.push_back(1); // version 1
          append_uleb(&alias_blob, export_aliases.size());
          for ( const std::string &alias : export_aliases )
          {
            append_uleb(&alias_blob, alias.size());
            alias_blob.insert(alias_blob.end(), alias.begin(), alias.end());
          }
          if ( !wasm_node.setblob_ea(alias_blob.data(), alias_blob.size(), start, 'E') )
            loader_failure("Cannot store WASM multi-export aliases at 0x%llX", uint64(start));
        }
      }
      std::vector<std::string> param_names;
      param_names.resize(type.params.size());
      for ( size_t p = 0; p < type.params.size(); ++p )
      {
        param_names[p] = format("a%zu", p + 1);
      }
      for ( size_t l = 0; l < module.local_names.size(); ++l )
      {
        if ( module.local_names[l].function_index == function_index )
        {
          for ( const auto &loc : module.local_names[l].locals )
          {
            if ( loc.local_index < type.params.size() && valid_identifier(loc.name) )
            {
              param_names[loc.local_index] = sanitize_identifier(loc.name);
            }
          }
          break;
        }
      }
      if ( !type.params.empty() && param_names[0] == "a1" )
      {
        // Only real member functions get `this`, and functions
        // with a hidden sret pointer get `__ret` at param 0 (plus `this` at
        // param 1 for methods).
        qstring demangled;
        if ( demangle_name(&demangled, name.c_str(), 0) > 0 )
        {
          const std::string text(demangled.c_str());
          const size_t paren = text.find('(');
          const std::string prefix = paren != std::string::npos ? text.substr(0, paren) : text;
          bool is_this = false;
          bool is_sret = false;
          const size_t wasm_params = type.params.size();
          if ( paren != std::string::npos )
          {
            const std::vector<std::string> dem_params = split_demangled_parameters(text);
            if ( prefix.find("::") != std::string::npos )
            {
              const bool ctor_dtor = name.find("C1E") != std::string::npos
                || name.find("C2E") != std::string::npos || name.find("D1E") != std::string::npos
                || name.find("D2E") != std::string::npos;
              if ( ctor_dtor )
                is_this = true;
              else if ( wasm_params == dem_params.size() + 1 )
                is_this = true;
              else if ( wasm_params == dem_params.size() + 2 && type.results.empty() )
              {
                is_sret = true;
                is_this = true;
              }
            }
            else if ( wasm_params == dem_params.size() + 1 && type.results.empty() )
            {
              is_sret = true;
            }
          }
          if ( is_sret )
          {
            param_names[0] = "__ret";
            if ( is_this && param_names.size() > 1 && param_names[1] == "a2" )
              param_names[1] = "this";
          }
          else if ( is_this )
          {
            param_names[0] = "this";
          }
        }
      }

      tinfo_t tif;
      bool has_prototype = false;
      go_xf_result_info_t go_xf_info;
      constexpr bool kGoXfApplyEnabled = false;
      const bool go_xf_eligible = !startup && go_xf_single_result(module, i, name, &go_xf_info);
      bool go_xf = kGoXfApplyEnabled && go_xf_eligible;
      if ( go_xf_eligible && !go_xf )
        ++go_xf_eligible_count;
      qstring go_xf_decl_head; // the x=f return head; empty unless go_xf holds
      if ( go_xf )
      {
        go_c_struct_t go_xf_struct;
        go_sig_param_t go_xf_param;
        go_xf_param.display = go_xf_info.c_name;
        go_xf_param.c_name = go_xf_info.c_name;
        go_xf_param.size = go_xf_info.extent - go_xf_info.min_offset;
        if ( !go_c_struct_for_param(module, go_xf_param, &go_xf_struct)
             || !ensure_go_struct_type(go_xf_struct.c_name.c_str(), go_xf_struct.fields,
                                       go_xf_struct.size) ) // NOLINT — memoized
          go_xf = false;
      }
      if ( !go_xf || go_xf_info.c_name.size() >= sizeof(go_xf_wire_t{}.c_name) )
        go_xf = false; // a name that cannot ride the wire keeps the sret view
      const bool go_display_multi_sret = display != &type && display->results.size() > 1;
      if ( go_display_multi_sret )
      {
        // Count the cross-check verdicts exactly where the forced
        // sret display is built — a proven-aligned slot keeps `result`, a
        // bound argument slot downgrades the name (and the named pointee).
        if ( sret_slot_verdict == go_sret_slot_verdict_t::kAligned )
          ++go_sret_slot_aligned;
        else if ( sret_slot_lie )
        {
          ++go_sret_slot_downgraded;
          if ( go_named )
            ++go_sret_slot_downgraded_named;
        }
      }
      if ( display->results.size() > 1 )
      {
        has_prototype = make_multi_prototype(
          *display, &tif, go_display_multi_sret,
          !sret_slot_lie && !go_sret_pointee.empty() ? go_sret_pointee.c_str() : nullptr,
          sret_slot_lie ? nullptr : "result");
        // x=f prototype: the named composite returns through the wasm
        // multi-return registers — the same scattered word table
        // make_multi_return_location lays out (multi_return_registers
        // {0,2,1,3,6,7}), spelled in the parse_decl `off:reg` syntax. The
        // Step C builder below replaces this sret prototype with the x=f
        // declaration (both sides of every call agree on the return type,
        // so the raw-i32 contract above is not in play); a
        // Step C failure falls back to the sret form built here.
        if ( has_prototype && go_xf )
        {
          static constexpr int go_xf_return_regs[6] = { 0, 2, 1, 3, 6, 7 };
          go_xf_decl_head.sprnt("%s __usercall dummy", go_xf_info.c_name.c_str());
          go_xf_decl_head.append("@<");
          size_t go_xf_word = 0;
          uint32 go_xf_byte = 0;
          for ( size_t k = 0; k < go_xf_info.slot_types.size(); ++k )
          {
            const uint32 bytes
              = go_xf_info.slot_types[k] == 0x7E || go_xf_info.slot_types[k] == 0x7C ? 8 : 4;
            for ( uint32 part = 0; part < bytes; part += 4 )
            {
              qstring piece;
              piece.sprnt("%s%u:r%d", k == 0 && part == 0 ? "" : ", ", go_xf_byte + part,
                          go_xf_return_regs[go_xf_word++]);
              go_xf_decl_head.append(piece);
            }
            go_xf_byte += bytes;
          }
          go_xf_decl_head.append(">");
        }
        else
          go_xf = false;
      }
      else
      {
        std::string decl;
        const char *known_decl = find_standard_import_prototype(qstring(name.c_str()), *display);
        if ( known_decl != nullptr )
        {
          decl = known_decl;
          has_prototype
            = parse_decl(&tif, nullptr, get_idati(), decl.c_str(), PT_SIL | PT_SEMICOLON);
        }
        if ( !has_prototype )
        {
          decl.clear();
          qstring decl_text;
          std::vector<qstring> param_name_texts;
          param_name_texts.reserve(param_names.size());
          for ( const std::string &param : param_names )
            param_name_texts.push_back(qstring(param.c_str()));
          // Prefer a prototype whose parameter TYPES come from the
          // demangled Itanium name (yyjson_read_opts gains `const
          // yyjson_alc *`, `yyjson_read_err *`, ...). Size-checked per
          // parameter; any miss falls back to the wasm-only prototype.
          {
            qstring mangled_demangled;
            if ( demangle_name(&mangled_demangled, name.c_str(), 0) > 0 )
            {
              const std::string text(mangled_demangled.c_str());
              const size_t paren = text.find('(');
              const std::string prefix = paren != std::string::npos ? text.substr(0, paren) : text;
              has_prototype
                = make_mangled_prototype(text, prefix, *display, &param_name_texts, &decl_text)
                && parse_decl(&tif, nullptr, get_idati(), decl_text.c_str(), PT_SIL | PT_SEMICOLON);
            }
          }
          if ( !has_prototype )
          {
            decl_text.clear();
            has_prototype
              = make_prototype(*display, qstring(name.c_str()), &decl_text, &param_name_texts)
              && parse_decl(&tif, nullptr, get_idati(), decl_text.c_str(), PT_SIL | PT_SEMICOLON);
          }
        }
      }
      if ( !has_prototype )
      {
        // A proven Go display signature that no prototype builder
        // accepts (e.g. param + tuple result past the user-register budget)
        // falls back to the RAW wasm signature instead of failing the load —
        // the typed display is best-effort, the RETUNWIND raw view is always
        // representable. '5'/'7' are re-stored with the raw result vector so
        // the decompiler's signature cross-check stays consistent.
        if ( display != &type )
        {
          display = &type;
          if ( !store_type_vector(wasm_node, start, '5', type.results)
               || !store_reference_metadata(wasm_node, start, '7', type.result_heap_types,
                                            type.result_ref_forms) )
            loader_failure("Cannot store WASM function signature for function %u", function_index);
          if ( type.results.size() > 1 )
          {
            has_prototype = make_multi_prototype(type, &tif);
          }
          else
          {
            std::string decl2;
            const char *known_decl2 = find_standard_import_prototype(qstring(name.c_str()), type);
            if ( known_decl2 != nullptr )
            {
              decl2 = known_decl2;
              has_prototype
                = parse_decl(&tif, nullptr, get_idati(), decl2.c_str(), PT_SIL | PT_SEMICOLON);
            }
            if ( !has_prototype )
            {
              qstring decl_text2;
              std::vector<qstring> param_name_texts2;
              param_name_texts2.reserve(param_names.size());
              for ( const std::string &param : param_names )
                param_name_texts2.push_back(qstring(param.c_str()));
              has_prototype
                = make_prototype(type, qstring(name.c_str()), &decl_text2, &param_name_texts2)
                && parse_decl(&tif, nullptr, get_idati(), decl_text2.c_str(),
                              PT_SIL | PT_SEMICOLON);
            }
          }
        }
      }
      if ( !has_prototype )
        loader_failure("Unsupported WASM function signature %u", function_index);
      if ( m1d2_go_frame && go_frame_bytes != 0 && module.go_types.present
           && i < module.go_types.arginfo.size() )
      {
        std::vector<go_shape_param_t> step_c_shape;
        const go_func_layout_t *step_c_layout = i < module.go_types.layouts.size()
            && module.go_types.layouts[i].source != go_layout_source_t::kNone
          ? &module.go_types.layouts[i]
          : nullptr;
        if ( step_c_layout != nullptr )
        {
          step_c_shape.reserve(step_c_layout->params.size());
          for ( const go_layout_param_t &record : step_c_layout->params )
            step_c_shape.push_back(
              go_shape_param_t{ record.is_group, record.offset, record.bytes });
        }
        else if ( !go_arginfo_shape_params(module.go_types.arginfo[i], &step_c_shape) )
          step_c_shape.clear();
        if ( step_c_shape.empty() )
        {
          ++go_step_c_shape_skip_count;
        }
        else
        {
          if ( display->results.size() >= 2 && !(go_xf && !go_xf_decl_head.empty()) )
          {
            ++go_step_c_sret_keep_count;
          }
          else
          {
            const go_named_sig_t *step_c_sig
              = go_named && go_named_sig.complete ? &go_named_sig : nullptr;
            qstring step_c_decl;
            // x=f: an eligible function returns its single named result
            // through the wasm multi-return registers (head pre-built with
            // the scattered `off:reg` words) instead of a scalar/void return.
            if ( go_xf && !go_xf_decl_head.empty() )
            {
              step_c_decl = go_xf_decl_head;
            }
            else
            {
              const char *step_c_ret = display->results.empty() ? "void"
                : display->results[0] == 0x7E                   ? "__int64"
                : display->results[0] == 0x7C                   ? "double"
                                                                : "int";
              // The declaration parses with a dummy name (Go function names
              // contain dots); apply_tinfo binds it to the already-named
              // function, exactly like make_prototype above.
              step_c_decl.sprnt("%s __usercall dummy", step_c_ret);
              if ( display->results.size() == 1 )
              {
                step_c_decl.append("@<");
                if ( display->results[0] == 0x7E )
                  step_c_decl.append("r0:r1");
                else if ( display->results[0] == 0x7C )
                  step_c_decl.append("xmm0");
                else
                  step_c_decl.append("r0");
                step_c_decl.append(">");
              }
            }
            // go-sig-drop-pcb: pc_b_register_proof proves local 0 is only
            // the resume-dispatcher selector / indirect-call temp — every
            // read is consumed by the lowering (dispatcher, PC_F
            // extract), so the emitted microcode never references the r0
            // incoming argument and the prototype omits the raw param0.
            // The window parameters then ARE the signature.
            const bool step_c_drop_pcb = body.go_ir.pc_b_register_proof;
            if ( step_c_drop_pcb )
            {
              step_c_decl.append("(");
              ++go_step_c_pcb_drop_count;
            }
            else
            {
              step_c_decl.append("(int a1@<r0>");
            }
            bool step_c_ok = true;
            size_t step_c_named = 0;
            // Typed receivers: an INCOMPLETE method signature (the
            // anchor-4 gap-cut form — `(*main.Ball) draw(...)`) still PROVES
            // its receiver: the gap-cut validated that the leading window
            // slots tile the receiver words. Type the FIRST parameter from
            // the receiver descriptor — a pointer receiver is one 8-byte
            // word (Go pointers are 8 bytes on wasm, matching the scalar
            // cell Step C already proved), a value receiver is the struct by
            // value at the descriptor size. Any disagreement keeps the raw
            // scalar (fail-closed, counted as receiver-skip).
            bool step_c_receiver_done = false;
            go_sig_param_t step_c_receiver;
            bool step_c_receiver_pointer = false;
            if ( step_c_sig == nullptr && go_named && !go_named_sig.complete
                 && !go_named_sig.receiver.empty()
                 && go_receiver_c_param(module, go_named_sig, &step_c_receiver_pointer,
                                        &step_c_receiver) )
            {
              const go_shape_param_t &lead = step_c_shape.front();
              const bool pointer_ok = step_c_receiver_pointer && !lead.is_group && lead.bytes == 8;
              const bool value_ok
                = !step_c_receiver_pointer && lead.is_group && lead.bytes == step_c_receiver.size;
              if ( pointer_ok || value_ok )
              {
                if ( ensure_step_c_receiver_struct(module, step_c_receiver, 1) )
                {
                  step_c_receiver_done = true;
                  ++go_step_c_receiver_count;
                }
                else
                  ++go_step_c_receiver_skip_count; // registration failed: raw scalar
              }
              else
                ++go_step_c_receiver_skip_count; // leading shape disagrees
            }
            else if ( options::debug("gocall") && step_c_sig == nullptr && go_named
                      && !go_named_sig.complete && !go_named_sig.receiver.empty() )
            {
              msg("wasmhex: step-C receiver unresolved %s recv=%s\n", name.c_str(),
                  go_named_sig.receiver.c_str());
            }
            for ( size_t k = 0; k < step_c_shape.size() && step_c_ok; ++k )
            {
              const go_shape_param_t &sp = step_c_shape[k];
              const bool sp_is_receiver = k == 0 && step_c_receiver_done;
              // The positional pairing with the complete named signature
              // — kept for the arity check and as the group-typing
              // FALLBACK when the tier-(a) record did not pair (the primary
              // type source is the record below).
              const go_sig_param_t *nparam
                = step_c_sig != nullptr && step_c_named < step_c_sig->params.size()
                ? &step_c_sig->params[step_c_named]
                : nullptr;
              if ( nparam != nullptr )
                ++step_c_named;
              // The group's type comes from the SAME layout record the
              // shape did — tier (a) paired the anchored signature params
              // to the shape entries by byte range at build time, so this
              // is no longer a positional join against the named sig. The
              // record carries c_name + the exact fields to register.
              const go_layout_param_t *step_c_record = step_c_layout != nullptr
                  && step_c_layout->source == go_layout_source_t::kDescriptor
                  && k < step_c_layout->params.size()
                  && step_c_layout->params[k].offset == sp.offset
                  && step_c_layout->params[k].bytes == sp.bytes
                ? &step_c_layout->params[k]
                : nullptr;
              qstring ptype;
              qstring pname;
              pname.sprnt("a%zu", step_c_drop_pcb ? k + 1 : k + 2);
              if ( sp_is_receiver )
              {
                if ( step_c_receiver_pointer )
                  ptype.sprnt("%s *", step_c_receiver.c_name.c_str());
                else
                  ptype = step_c_receiver.c_name.c_str();
              }
              else if ( sp.is_group )
              {
                if ( step_c_record != nullptr && !step_c_record->c_name.empty() )
                {
                  if ( !step_c_record->fields.empty() )
                  {
                    if ( !ensure_go_struct_type(step_c_record->c_name.c_str(),
                                                step_c_record->fields, step_c_record->bytes) )
                    {
                      step_c_ok = false;
                      break;
                    }
                  }
                  else if ( step_c_record->c_name != "go__string"
                            && step_c_record->c_name != "go__slice"
                            && step_c_record->c_name != "go__iface" )
                  {
                    // A named composite whose field walk did not close —
                    // exactly the case the old go_c_struct_for_param path
                    // failed: the window cell stays a local (fail closed).
                    step_c_ok = false;
                    break;
                  }
                  ptype = step_c_record->c_name.c_str();
                }
                else if ( nparam != nullptr && nparam->size == sp.bytes && !nparam->c_name.empty() )
                {
                  // No tier-(a) record entry (anchored pairing failed at
                  // build time — e.g. sig/shape arity mismatch): the
                  // positional fallback path stays, byte-checked per entry.
                  go_c_struct_t cstr;
                  if ( !go_c_struct_for_param(module, *nparam, &cstr)
                       || !ensure_go_struct_type(nparam->c_name.c_str(), cstr.fields, cstr.size) )
                  {
                    step_c_ok = false;
                    break;
                  }
                  ptype = nparam->c_name.c_str();
                }
                else
                {
                  // No proven composite type for this group — the window
                  // cell stays a local; fail closed for the whole signature.
                  step_c_ok = false;
                  break;
                }
              }
              else if ( sp.bytes == 4 )
              {
                ptype = "int";
              }
              else if ( sp.bytes == 8 )
              {
                ptype = "__int64";
              }
              else
              {
                step_c_ok = false;
                break;
              }
              // go-param-name: a complete named signature paired this shape
              // entry (positionally, byte-checked where a type rode on it)
              // — its curated parameter name (known-API: this/method/args,
              // format/args, ...) replaces the aN placeholder. Fail-closed:
              // byte-disagreement or an unnamed entry keeps aN.
              if ( !sp_is_receiver && nparam != nullptr && nparam->size == sp.bytes
                   && valid_identifier(nparam->name) )
              {
                pname = nparam->name.c_str();
              }
              qstring piece;
              if ( k == 0 && step_c_drop_pcb )
                piece.sprnt("%s %s", ptype.c_str(), pname.c_str());
              else
                piece.sprnt(", %s %s", ptype.c_str(), pname.c_str());
              step_c_decl.append(piece);
            }
            if ( step_c_ok && step_c_sig != nullptr && step_c_named != step_c_sig->params.size() )
              step_c_ok = false; // named arity disagrees with the proven shape
            if ( step_c_ok )
            {
              step_c_decl.append(");");
              tinfo_t step_c_tif;
              // x=f: parse_decl cannot spell a scattered return location
              // (it is a print-only form), so an x=f declaration parses as the
              // void form and the return type + scattered retloc are grafted
              // afterwards via func_type_data_t — the same route
              // make_multi_prototype's non-sret path uses for wasm tuples.
              qstring step_c_parse_decl = step_c_decl;
              if ( go_xf && !go_xf_decl_head.empty() )
              {
                const size_t call_at = step_c_decl.find('(');
                step_c_parse_decl.sprnt("void __usercall dummy%s",
                                        step_c_decl.c_str()
                                          + (call_at == qstring::npos ? 0 : size_t(call_at)));
              }
              if ( parse_decl(&step_c_tif, nullptr, get_idati(), step_c_parse_decl.c_str(),
                              PT_SIL | PT_SEMICOLON) )
              {
                bool step_c_xf_applied = false;
                if ( go_xf && !go_xf_decl_head.empty() )
                {
                  func_type_data_t xf_details;
                  tinfo_t xf_named;
                  if ( step_c_tif.get_func_details(&xf_details, GTD_CALC_ARGLOCS)
                       && xf_details.rettype.is_void()
                       && xf_named.get_named_type(get_idati(), go_xf_info.c_name.c_str(),
                                                  BTF_STRUCT)
                       && xf_named.is_udt()
                       && xf_named.get_size() == go_xf_info.extent - go_xf_info.min_offset
                       && make_multi_return_location(go_xf_info.slot_types, &xf_details.retloc) )
                  {
                    xf_details.rettype = xf_named;
                    // The explicit locations must survive create_func — without
                    // FTI_EXPLOCS the engine ignores the scattered retloc (the
                    // make_wasm_function_type_uncached rule).
                    xf_details.set_cc(CM_CC_SPECIAL);
                    xf_details.flags |= FTI_ARGLOCS | FTI_EXPLOCS;
                    step_c_xf_applied = step_c_tif.create_func(xf_details);
                  }
                }
                if ( !go_xf || step_c_xf_applied )
                {
                  if ( go_xf && options::debug("gocall") )
                  {
                    func_type_data_t xf_check;
                    qstring xf_rendered = "?";
                    if ( step_c_tif.get_func_details(&xf_check, GTD_CALC_ARGLOCS) )
                    {
                      qstring ret_type_str;
                      if ( xf_check.rettype.print(&ret_type_str, nullptr, PRTYPE_1LINE) )
                        xf_rendered = ret_type_str;
                      msg("wasmhex: x=f parsed %s ret=\"%s\" loc=%d\n", name.c_str(),
                          xf_rendered.c_str(), int(xf_check.retloc.atype()));
                    }
                  }
                  tif = step_c_tif;
                  ++go_step_c_params_count;
                  // x=f marker only now that the prototype actually applied —
                  // a parse rejection can never strand a stale 'U' record.
                  if ( go_xf )
                  {
                    go_xf_wire_t go_xf_wire = {};
                    go_xf_wire.version = 1;
                    go_xf_wire.slot_count = uint8(go_xf_info.slot_types.size());
                    go_xf_wire.min_offset = go_xf_info.min_offset;
                    go_xf_wire.extent = go_xf_info.extent;
                    std::memcpy(go_xf_wire.c_name, go_xf_info.c_name.c_str(),
                                go_xf_info.c_name.size() + 1);
                    if ( !wasm_node.supset_ea(start, &go_xf_wire, sizeof(go_xf_wire), 'U') )
                      loader_failure("Cannot store the WASM Go x=f result record at 0x%llX",
                                     uint64(start));
                    ++go_xf_results_count;
                  }
                }
              }
              else
              {
                // Debug: the exact declaration parse_decl rejected (the
                // counted parse-skip bucket needs a per-function reason).
                if ( options::debug("gocall") )
                  msg("wasmhex: go frame step-C parse-skip %s: \"%s\"\n", name.c_str(),
                      step_c_decl.c_str());
                ++go_step_c_parse_skip_count;
              }
            }
            else
            {
              // Debug: which shape cell failed (untyped group / scalar
              // width / named arity) for the counted shape-skip bucket.
              if ( options::debug("gocall") )
                msg("wasmhex: go frame step-C shape-skip %s: groups=%zu sig=%p arity=%zu/%zu\n",
                    name.c_str(), step_c_shape.size(), static_cast<const void *>(step_c_sig),
                    step_c_named, step_c_sig != nullptr ? step_c_sig->params.size() : size_t(0));
              ++go_step_c_shape_skip_count;
            }
          }
        }
      }
      apply_tinfo(start, tif, TINFO_DEFINITE);
    }
  }

  // Evidence-based renaming of proven js.Value
  // receiver globals. The core walk (go_jsvalue_receiver_globals) proved
  // 16-byte data slots whose both words are loaded as the receiver pair
  // of a known-API syscall/js call; each becomes `g_<hex>` typed as the
  // registered go__js__Value struct. Fail-closed: an address that already
  // carries a real (non-auto) name keeps it, and a missing struct
  // registration skips the typing (name only).
  size_t go_global_name_count = 0;
  size_t go_global_skip_count = 0;
  if ( module.go_provenance )
  {
    std::vector<uint64> js_globals;
    if ( go_jsvalue_receiver_globals(module, &js_globals) )
    {
      tinfo_t js_value_type;
      const bool js_value_ok
        = js_value_type.get_named_type(get_idati(), "go__js__Value", BTF_STRUCT)
        && js_value_type.is_udt() && js_value_type.get_size() == 16;
      for ( uint64 address : js_globals )
      {
        const ea_t ea = ea_t(wasmhex_memory_base + address);
        if ( ea == BADADDR )
        {
          ++go_global_skip_count;
          continue;
        }
        qstring existing;
        get_name(&existing, ea);
        const bool named = !existing.empty() && !is_uname(existing.c_str());
        if ( named )
        {
          ++go_global_skip_count;
          continue;
        }
        char name_buf[32];
        qsnprintf(name_buf, sizeof(name_buf), "g_%llX", (unsigned long long)address);
        if ( js_value_ok )
        {
          // A 16-byte struct item: qword head (the tail word merges into
          // the typed item once the struct applies).
          if ( !create_data(ea, qword_flag(), 16, BADNODE) )
          {
            ++go_global_skip_count;
            continue;
          }
          apply_tinfo(ea, js_value_type, TINFO_DEFINITE);
        }
        set_name(ea, name_buf, SN_FORCE | SN_NODUMMY);
        ++go_global_name_count;
      }
    }
  }
  if ( go_global_name_count != 0 || go_global_skip_count != 0 )
    msg("wasmhex: go js.Value receiver globals: named=%zu skip=%zu\n", go_global_name_count,
        go_global_skip_count);

  {
    std::vector<wasmhex::loader::mem_dref_t> mem_records;
    std::vector<wasmhex::loader::global_ref_t> global_records;
    size_t mem_dref_count = 0;
    size_t global_dref_count = 0;
    if ( const_mem_drefs(module, &mem_records, &global_records) != 0 )
    {
      const uint64 memory_limit = maps_linear_segment ? linear_memory_bytes : 0;
      for ( const wasmhex::loader::mem_dref_t &record : mem_records )
      {
        if ( record.address >= memory_limit )
          continue; // outside the mapped extent: no item exists there
        const ea_t from = code_base
          + (startup_helpers[record.body_index] != nullptr
               ? startup_offsets[record.body_index]
               : module.bodies[record.body_index].offset)
          + record.insn_offset;
        const ea_t to = get_item_head(ea_t(wasmhex_memory_base + record.address));
        if ( from == BADADDR || to == BADADDR || from == to )
          continue;
        const uchar kind = record.store ? uchar(2) : uchar(1);
        if ( wasm_node.supset_ea(from, &to, sizeof(to), '8')
             && wasm_node.supset_ea(from, &kind, sizeof(kind), '9') )
          ++mem_dref_count;
      }
      for ( const wasmhex::loader::global_ref_t &record : global_records )
      {
        ea_t to = BADADDR;
        if ( wasm_node.supval(record.global_index, &to, sizeof(to), 'j') != sizeof(to) )
          continue; // imported or unmapped global: no item to reference
        if ( to == BADADDR || to == 0 )
          continue;
        const ea_t from = code_base
          + (startup_helpers[record.body_index] != nullptr
               ? startup_offsets[record.body_index]
               : module.bodies[record.body_index].offset)
          + record.insn_offset;
        if ( from == BADADDR || from == to )
          continue;
        const uchar kind = 3; // global.get reference (read)
        if ( wasm_node.supset_ea(from, &to, sizeof(to), '8')
             && wasm_node.supset_ea(from, &kind, sizeof(kind), '9') )
          ++global_dref_count;
      }
    }
    msg("wasmhex: const-address data references: memory=%zu globals=%zu\n", mem_dref_count,
        global_dref_count);
  }

  {
    const std::uint64_t arg_memory_limit = maps_linear_segment ? linear_memory_bytes : 0;
    if ( arg_memory_limit != 0 )
    {
      std::vector<std::uint64_t> table_masks(
        module.imported_func_types.size() + module.bodies.size(), 0);
      for ( size_t i = 0; i < module.function_imports.size(); ++i )
        table_masks[i] = libc_pointer_mask(module.function_imports[i].name);
      for ( const export_t &export_entry : module.function_names ) // exports + DWARF names
        if ( export_entry.index < table_masks.size() )
          table_masks[export_entry.index] |= libc_pointer_mask(export_entry.name);
      const param_pointer_result_t pointers
        = param_pointer_walk(module, table_masks, arg_memory_limit);
      // Blob '4': {u32 magic, u32 count, u64 masks[count]} indexed by FUNCTION
      // index (imports first, always zero) so the decompiler resolves a call
      // site without knowing the import count.
      if ( !module.bodies.empty() )
      {
        const size_t total_functions = table_masks.size();
        const uint32 pointer_blob_header[2] = { 0x50505243u, uint32(total_functions) };
        std::vector<uchar> pointer_blob;
        pointer_blob.resize(sizeof(pointer_blob_header) + total_functions * sizeof(std::uint64_t));
        std::memcpy(pointer_blob.data(), pointer_blob_header, sizeof(pointer_blob_header));
        std::uint64_t *const mask_view
          = reinterpret_cast<std::uint64_t *>(pointer_blob.data() + sizeof(pointer_blob_header));
        for ( size_t i = 0; i < total_functions; ++i )
          mask_view[i] = 0;
        for ( size_t i = 0; i < pointers.proven_param_masks.size() && i < module.bodies.size();
              ++i )
          mask_view[module.imported_func_types.size() + i] = pointers.proven_param_masks[i];
        wasm_node.supset(0, pointer_blob.data(), pointer_blob.size(),
                         uchar(wasmhex::tag_t::param_pointer_masks));
      }
      size_t arg_dref_count = 0;
      size_t proven_functions = 0;
      for ( const std::uint64_t mask : pointers.proven_param_masks )
        if ( mask != 0 )
          ++proven_functions;
      // Group the records per call instruction EA: one kind-4 dref ('8'/'9',
      // item creation) per constant + one blob '3' carrying the
      // (param_index, address) pairs the decompiler swap consumes. The
      // address must land in the initialized span or the BSS ABOVE it — a
      // constant below the span start stays numeric (creating an item there
      // would re-anchor every unrelated sp-relative display).
      std::map<ea_t, std::vector<std::pair<std::uint32_t, std::uint64_t>>> per_call_args;
      for ( const pointer_arg_record_t &record : pointers.arg_records )
      {
        if ( record.address >= arg_memory_limit || record.address < linear_span_begin )
          continue; // outside [span begin, memory extent): no honest item there
        const ea_t from = code_base
          + (startup_helpers[record.body_index] != nullptr
               ? startup_offsets[record.body_index]
               : module.bodies[record.body_index].offset)
          + record.insn_offset;
        const ea_t to = ea_t(wasmhex_memory_base + record.address);
        if ( from == BADADDR || to == BADADDR || from == to )
          continue;
        const uchar kind = 4; // constant pointer argument at a proven position
        if ( wasm_node.supset_ea(from, &to, sizeof(to), '8')
             && wasm_node.supset_ea(from, &kind, sizeof(kind), '9') )
          ++arg_dref_count;
        per_call_args[from].emplace_back(record.param_index, record.address);
      }
      for ( const auto &call_args : per_call_args )
      {
        if ( call_args.second.empty() || call_args.second.size() > 255 )
          continue;
        std::vector<uchar> blob(8 + call_args.second.size() * 16);
        const uint32 header = 0x52475241u; // 'ARGR'
        std::memcpy(blob.data(), &header, sizeof(header));
        blob[4] = uchar(call_args.second.size());
        for ( size_t e = 0; e < call_args.second.size(); ++e )
        {
          uchar *entry = blob.data() + 8 + e * 16;
          entry[0] = uchar(call_args.second[e].first);
          uint64 address = call_args.second[e].second;
          std::memcpy(entry + 8, &address, sizeof(address));
        }
        wasm_node.supset_ea(call_args.first, blob.data(), blob.size(),
                            uchar(wasmhex::tag_t::pointer_arg_records));
      }
      msg("wasmhex: pointer-parameter proof: functions=%zu/%zu arg-drefs=%zu\n", proven_functions,
          pointers.proven_param_masks.size(), arg_dref_count);
    }
  }

  for ( const module_t::elem_entry_t &elem : module.elem_entries )
  {
    const ea_t target_ea = wasm_node.eaget_idx(elem.function_index, 'F');
    if ( target_ea != BADADDR && target_ea != 0 )
      wasm_node.easet_idx(elem.slot_index, target_ea, 'E');
    wasm_node.supset(elem.slot_index, &elem.function_index, sizeof(elem.function_index), 'f');
  }

  // One summary line for the named-signature layer (fail-closed
  // a module without anchors reports zeros, never guesses).
  if ( module.go_types.present )
    msg("wasmhex: go named signatures: itab=%zu known-api=%zu funcof-callback=%zu "
        "descriptor-method=%zu (named sret prototypes=%zu)\n",
        go_named_counts[0], go_named_counts[1], go_named_counts[2], go_named_counts[3],
        go_named_sret_count);
  // The single-source layout census — how many defined
  // bodies carry each confidence tier (descriptor anchor / plain ArgInfo
  // shape / shape + size-agreement names) and why the rest skipped. The
  // record is the one source the prototype path and every call site read.
  if ( module.go_types.present && !module.go_types.layouts.empty() )
  {
    const go_layout_census_t &census = module.go_types.layout_census;
    msg("wasmhex: go func layout (descriptor > arginfo > evidence): descriptor=%zu "
        "arginfo=%zu evidence=%zu skip-no-arginfo=%zu skip-shape=%zu (total=%zu)\n",
        census.descriptor, census.arginfo, census.evidence, census.skip_no_arginfo,
        census.skip_shape, census.total);
  }
  if ( go_step_c_params_count != 0 || go_step_c_shape_skip_count != 0
       || go_step_c_parse_skip_count != 0 || go_step_c_sret_keep_count != 0
       || go_step_c_pcb_drop_count != 0 )
    msg("wasmhex: go frame step-C params: upgraded=%zu shape-skip=%zu parse-skip=%zu "
        "sret-keep=%zu pcb-drop=%zu\n",
        go_step_c_params_count, go_step_c_shape_skip_count, go_step_c_parse_skip_count,
        go_step_c_sret_keep_count, go_step_c_pcb_drop_count);
  // Typed receivers: incomplete method signatures whose proven
  // receiver descriptor typed the leading window parameter.
  if ( go_step_c_receiver_count != 0 || go_step_c_receiver_skip_count != 0 )
    msg("wasmhex: go frame step-C receivers: typed=%zu skip=%zu\n", go_step_c_receiver_count,
        go_step_c_receiver_skip_count);
  // x=f single results (fail-closed counter; every x=f function also
  // carries the Step C window parameters and a 'U' wire record).
  if ( true )
    msg("wasmhex: go x=f single results: enabled=%zu eligible-pending=%zu\n", go_xf_results_count,
        go_xf_eligible_count);
  // The sret result-slot cross-check verdicts — `downgraded` counts
  // forced-sret displays whose phantom `result` register slot is bound by a
  // caller ArgInfo argument group at a non-result window offset (reason:
  // positional join lie, e.g. fmt.Sprintf's variadic slice); those keep the
  // pointer type but lose the `result` name/pointee. `named` is the subset
  // that had a name-anchor result suppressed by the same check.
  if ( module.go_provenance )
    msg("wasmhex: go sret result-slot cross-check: aligned=%zu downgraded=%zu (named=%zu)\n",
        go_sret_slot_aligned, go_sret_slot_downgraded, go_sret_slot_downgraded_named);

  create_filename_cmt();
  // The interprocedural this-types pass was removed (dead code, no
  // caller); nothing runs here at load — see netnode_tags.hpp for the
  // retired 'I' latch slot.
}

// Typed receivers: register a receiver struct AND the canonical
// named field types it references before the prototype parses —
// ensure_go_struct_type parses each field's C type, so a named field whose
// struct is not in the til yet (go__string in main.Ball's color) would
// fail the whole registration. Only the canonical carriers are
// pre-registered; any other named field type falls through to the struct
// registration itself (fail-closed, counted as receiver-skip).
static bool ensure_step_c_receiver_struct(const module_t &module, const go_sig_param_t &param,
                                          int depth)
{
  go_c_struct_t layout;
  if ( !go_c_struct_for_param(const_cast<module_t &>(module), param, &layout)
       || layout.fields.empty() )
    return false;
  // Split padding wider than one word into 8-byte carriers — a `__int64`
  // member cannot carry a 16-byte size through create_udt (main.Ball's
  // 16-byte hole after y failed exactly here while pad-free structs
  // registered fine).
  {
    std::vector<loader::go_c_field_t> split;
    split.reserve(layout.fields.size());
    for ( const loader::go_c_field_t &field : layout.fields )
    {
      if ( field.c_type == "__int64" && field.name.rfind("pad", 0) == 0 && field.size > 8 )
      {
        const std::uint64_t begin = field.offset;
        const std::uint64_t end = field.offset + field.size;
        for ( std::uint64_t off = begin; off < end; off += 8 )
          split.push_back(
            { "pad" + std::to_string(off), "__int64", off, std::min<std::uint64_t>(8, end - off) });
      }
      else
        split.push_back(field);
    }
    layout.fields = std::move(split);
  }
  for ( const go_c_field_t &field : layout.fields )
  {
    const std::string &c_type = field.c_type;
    if ( c_type == "__int64" || c_type == "int" || c_type == "bool" || c_type == "float"
         || c_type == "double" )
      continue;
    std::string base = c_type;
    if ( base.rfind("struct ", 0) == 0 )
      base.erase(0, 7); // "struct go__string" — the base name is the dependency
    if ( base.size() > 2 && base.compare(base.size() - 2, 2, " *") == 0 )
      base.erase(base.size() - 2); // pointer fields need the base type too
    if ( base != "go__string" && base != "go__slice" && base != "go__iface"
         && base != "go__js__Value" )
      continue;
    go_sig_param_t canonical;
    canonical.display = base == "go__string" ? "string"
      : base == "go__slice"                  ? "[]any"
      : base == "go__iface"                  ? "any"
                                             : "js.Value";
    canonical.c_name = base;
    go_c_struct_t canonical_layout;
    if ( !go_c_struct_for_param(const_cast<module_t &>(module), canonical, &canonical_layout)
         || !ensure_go_struct_type(base.c_str(), canonical_layout.fields, canonical_layout.size) )
      return false;
  }
  const bool final_ensure
    = ensure_go_struct_type(layout.c_name.c_str(), layout.fields, layout.size);
  return final_ensure;
}

// Resolve one named composite by display name and register it (bounded
// depth). Returns false when the descriptor is not in the layer or the
// registration fails — callers keep their previous state.
static bool ensure_step_c_struct(const module_t &module, const std::string &display,
                                 const std::string &c_name, int depth)
{
  if ( depth < 0 )
    return false;
  go_sig_param_t param;
  param.display = display;
  param.c_name = c_name;
  return ensure_step_c_receiver_struct(module, param, depth);
}

} // namespace wasmhex::loader

using namespace wasmhex::loader;

loader_t LDSC = { IDP_INTERFACE_VERSION, 0, accept_file, load_file, nullptr, nullptr, nullptr };
