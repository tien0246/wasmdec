// Pure standard-C++17 core of the wasmhex WebAssembly front end.
// This header (and everything under core/) must include ONLY
// standard headers — no IDA SDK types (qvector/qstring/int64 typedefs) — so
// the parser, validator and control scanner can be unit-tested with a plain
// compiler and reused by the loader through the border types below. The
// loader/ side keeps its IDA-facing interfaces and converts at the boundary.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "diagnostic.hpp"

#include "toolchains/emscripten/libcxx.hpp"
#include "toolchains/go/eh.hpp"
#include "toolchains/go/ir.hpp"
#include "toolchains/go/profile.hpp"
#include "toolchains/go/types.hpp"

#include "binary/leb128.hpp"
#include "binary/reader.hpp"

namespace wasmhex::loader
{

// Scalar aliases (the code base historically used the IDA SDK typedef
// spellings; inside this namespace they are plain fixed-width integers).
using uchar = std::uint8_t;
using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using bytevec = std::vector<uchar>;

constexpr size_t max_module_size = 256 * 1024 * 1024;

// Growable-memory ceiling (mirrors wasmhex_max_growable_memory_pages from
// ida/frame.hpp, which stays global-namespace for the processor module).
constexpr uint64 max_growable_memory_pages = 65536;

// WebAssembly value-type decoder (shared with the processor module)
namespace wasmhex_vt
{

constexpr std::int64_t heap_type_none = INT64_MIN;

constexpr std::int64_t heap_func = -0x10;
constexpr std::int64_t heap_extern = -0x11;
constexpr std::int64_t heap_eq = -0x13;
constexpr std::int64_t heap_i31 = -0x14;
constexpr std::int64_t heap_struct = -0x15;
constexpr std::int64_t heap_array = -0x16;

enum ref_kind_t : std::uint8_t
{
  ref_kind_none = 0,     // not a reference type
  ref_kind_nullable = 1, // (ref null ht), also the 0x6A..0x6D shorthand refs
  ref_kind_abs = 2,      // (ref ht) — non-null
};

enum value_type_status_t : std::uint8_t
{
  vt_ok = 0,
  vt_truncated = 1,        // input ended inside the value type
  vt_unsupported_heap = 2, // negative heap type outside the supported set
};

struct value_type_t
{
  std::uint8_t ref_kind = ref_kind_none;
  std::int64_t heap_type = heap_type_none;
  std::uint8_t canonical_type = 0;

  bool is_reference() const { return ref_kind != ref_kind_none; }
  bool has_abstract_heap() const { return heap_type != heap_type_none && heap_type < 0; }
  bool has_indexed_heap() const { return heap_type >= 0; }
};

inline bool supported_abstract_heap_type(std::int64_t heap_type)
{
  return heap_type == heap_func || heap_type == heap_extern
    || (heap_type >= heap_array && heap_type <= heap_eq);
}

// read_sleb33 moved to binary/leb128.hpp.

// Decodes exactly one WebAssembly value type starting at *pos:
//   0x7F/0x7E/0x7D/0x7C/0x70/0x6F  scalar or shorthand ref, no heap payload
//   0x6A..0x6D                     shorthand eq/struct/i31/array refs
//   0x63 <s33 heap type>           (ref null ht)
//   0x64 <s33 heap type>           (ref ht)
// Transactional: on vt_ok both *pos and *out are updated, otherwise neither
// changes. Whitelisting of the canonical byte stays with the caller.
inline value_type_status_t decode_value_type(const std::uint8_t *data, std::size_t size,
                                             std::size_t *pos, value_type_t *out)
{
  if ( out == nullptr || pos == nullptr || data == nullptr )
    return vt_truncated;
  std::size_t local = *pos;
  if ( local >= size )
    return vt_truncated;
  const std::uint8_t encoded = data[local++];

  value_type_t decoded;
  decoded.canonical_type = encoded;
  if ( encoded == 0x63 || encoded == 0x64 )
  {
    std::int64_t heap = 0;
    if ( !read_sleb33(data, size, &local, &heap) )
      return vt_truncated;
    if ( heap < 0 && !supported_abstract_heap_type(heap) )
      return vt_unsupported_heap;
    decoded.ref_kind = encoded == 0x63 ? ref_kind_nullable : ref_kind_abs;
    decoded.heap_type = heap;
    decoded.canonical_type = heap == heap_extern ? std::uint8_t(0x6F)
      : heap == heap_i31                         ? std::uint8_t(0x72)
      : (heap >= heap_array && heap <= heap_eq)  ? std::uint8_t(0x71)
                                                 : std::uint8_t(0x70);
  }
  else if ( encoded >= 0x6A && encoded <= 0x6D )
  {
    static constexpr std::int64_t shorthand_heaps[4] = {
      heap_array,
      heap_struct,
      heap_i31,
      heap_eq,
    };
    decoded.ref_kind = ref_kind_nullable;
    decoded.heap_type = shorthand_heaps[encoded - 0x6A];
    decoded.canonical_type = encoded == 0x6C ? std::uint8_t(0x72) : std::uint8_t(0x71);
  }
  *out = decoded;
  *pos = local;
  return vt_ok;
}

} // namespace wasmhex_vt

constexpr int64 no_heap_type = wasmhex_vt::heap_type_none;

// Shared metadata enums formerly defined in ida/call_metadata.hpp and
// ida/frame.hpp (both pure <cstdint>; the ida/ headers now re-export them).
constexpr uint32 WASMHEX_MAX_SPARSE_TABLE_DISPATCH_CASES = 65536;
// Upper bound on go1.26 call-protocol records accepted per function
// (blob 'x'); beyond this the plumbing renders as before (fail-closed).
constexpr uint32 WASMHEX_MAX_GO_CALL_RECORDS = 4096;

enum class wasmhex_call_trap_t : std::uint8_t
{
  none,
  bounds,
  null_reference,
  type_mismatch,
};

struct wasmhex_sparse_dispatch_t
{
  uint32 table_size;
  uint32 target_count;
};

enum class wasmhex_bounds_mode_t : int
{
  hidden = 0,
  helper = 1,
  explicit_bounds = 2,
};

enum class wasmhex_brtable_mode_t : int
{
  chain = 0,
  jtbl = 1,
};

struct wasmhex_c_frame_metadata_t
{
  std::uint32_t sp_global_index = 0;
  std::uint32_t fp_local_index = 0;
  std::uint32_t c_frame_bytes = 0;
  std::int8_t direction = -1;
  std::uint8_t reserved[3] = { 0, 0, 0 };
};
static_assert(sizeof(wasmhex_c_frame_metadata_t) == 16,
              "wasmhex_c_frame_metadata_t must have a fixed 16-byte binary layout");

// reader_t moved to binary/reader.hpp.

// Module data model
struct wasm_type_t
{
  std::vector<uchar> params;
  std::vector<uchar> results;
  std::vector<int64> param_heap_types;
  std::vector<int64> result_heap_types;
  std::vector<uchar> param_ref_forms;
  std::vector<uchar> result_ref_forms;
};

bool is_abstract_reference_handle(uchar type, int64 heap_type);

size_t wasm_results_byte_size(const std::vector<uchar> &results);

struct body_t
{
  uint64 offset = 0;
  uint32 size = 0;
  // Code-section payload range of this function's body (right after the
  // body-size LEB, spanning local groups + opcodes). LLVM/lld emits DWARF
  // DW_AT_low_pc pointing exactly at body start; a low_pc pointing at the
  // first opcode still falls inside this range. See loader/dwarf.cpp.
  uint32 source_offset = 0;
  uint32 source_end = 0;
  struct memory_access_t
  {
    uint32 offset = 0;
    uint64 memarg = 0;
    uchar width = 0;
    uchar scalar_opcode = 0;
    bool store = false;
    bool lane = false;
    bool scalar = false;
  };
  struct memory_trap_edge_t
  {
    uint32 check_offset = 0;
    uint32 access_offset = 0;
    uint32 stub_offset = 0;
    uint64 memarg = 0;
    bool store = false;
    bool scalar = false;
  };
  std::vector<memory_access_t> memory_accesses;
  std::vector<memory_trap_edge_t> memory_trap_edges;
  struct resolved_call_t
  {
    struct table_case_t
    {
      uint32 compare_offset = 0;
      uint32 stub_offset = 0;
      uint32 function_index = 0;
      uint32 slot_index = 0;
      wasmhex_call_trap_t trap = wasmhex_call_trap_t::none;
    };
    uint32 offset = 0;
    uint32 after = 0;
    uint32 function_index = 0;
    wasmhex_call_trap_t trap = wasmhex_call_trap_t::none;
    bool dynamic_table_call = false;
    bool sparse_table_call = false;
    bool reference_dispatch = false;
    bool tail_dispatch = false;
    bool table64_index = false;
    uint32 table_index = 0;
    uint32 type_index = 0;
    uint32 table_size = 0;
    uint64 dynamic_table_size = 0;
    uint32 target_count = 0;
    uint32 dispatch_offset = 0;
    uint32 default_offset = 0;
    uint32 bounds_check_offset = UINT32_MAX;
    uint32 bounds_offset = UINT32_MAX;
    std::vector<table_case_t> table_cases;
  };
  std::vector<resolved_call_t> resolved_calls;
  struct direct_call_t
  {
    uint32 offset = 0;
    uint32 function_index = 0;
  };
  std::vector<direct_call_t> direct_calls;
  struct eh_try_t
  {
    uint32 offset = 0;
    uint32 body = 0;
    uint32 end = UINT32_MAX;
    uint32 catch_offset = UINT32_MAX;
    uint32 handler_offset = UINT32_MAX;
    uint32 tag_index = UINT32_MAX;
    uchar payload_type = 0x40;
    uint32 throw_offset = UINT32_MAX;
    uint32 throw_tag_index = UINT32_MAX;
    uint32 payload_source_offset = UINT32_MAX;
    uint32 payload_source_after = UINT32_MAX;
    uint32 call_offset = UINT32_MAX;
    uint32 call_after = UINT32_MAX;
    uint32 function_index = UINT32_MAX;
    bool has_unreachable = false;
  };
  std::vector<eh_try_t> eh_tries;
  bool has_throw = false;
  uint32 throw_count = 0;
  uchar unconditional_throw_payload_type = 0x40;
  uint32 unconditional_throw_tag = UINT32_MAX;
  std::vector<uchar> local_types;
  std::vector<int64> local_heap_types;
  std::vector<uchar> local_ref_forms;
  size_t max_tuple_call_bytes = 0;
  bool has_c_frame = false;
  wasmhex_c_frame_metadata_t c_frame;
  const char *c_frame_fallback_reason = nullptr;
  struct control_t
  {
    uchar opcode = 0;
    uint32 offset = 0;
    uint32 after = UINT32_MAX;
    std::vector<uint32> targets;
    std::vector<uint32> depths;
    std::vector<uint32> stubs;
    std::vector<uchar> params;
    std::vector<uchar> types;
    std::vector<uchar> labels;
    std::vector<int64> param_heap_types;
    std::vector<int64> type_heap_types;
    std::vector<int64> label_heap_types;
    std::vector<uchar> param_ref_forms;
    std::vector<uchar> type_ref_forms;
    std::vector<uchar> label_ref_forms;
    uint32 source = UINT32_MAX;
    uint32 edge_index = 0;
    uint32 dispatch = UINT32_MAX;
    bool scope_end = false;
  };
  std::vector<control_t> control;
  // The call-site protocol sequence:
  //   PC_B=SP (local.set 0) ; local.get SP ; i64.const ret ;
  //   i64.store a3 o0 ; i32.const 0 ; call fn ; global.get SP ;
  //   local.set SP ; br_if unwind ; end(resume inline)
  // scanned byte-exact by scan_go_calls; the loader stores it as blob 'x'
  // so the decompiler can hide the goroutine plumbing.
  struct go_arg_t
  {
    uint32 opcode = 0;                // 0x37 i64 / 0x36 i32 / 0x2C f32 / 0x39 f64
    uint32 sp_offset = 0;             // store memarg offset (args area base)
    uint32 store_offset = UINT32_MAX; // store opcode offset (in body)
  };
  struct go_call_t
  {
    uint32 store_offset = UINT32_MAX; // i64.store (return-address push)
    uint32 call_offset = UINT32_MAX;  // call
    uint32 fn_index = UINT32_MAX;
    uint32 sp_restore_offset = UINT32_MAX; // global.get SP right after
    uint32 br_offset = UINT32_MAX;         // br_if unwind
    std::vector<go_arg_t> args;            // Argument stores preceding the SP-=8
  };
  std::vector<go_call_t> go_calls;
  // Go function IR built once at module load time when the Go
  // profile is verified (modern Go wasm).
  go_function_ir_t go_ir;
  // Emscripten EH evidence (invoke sites + __THREW__ protocol +
  // landing pads) built once at module load time for non-Go modules with
  // invoke_* imports. Empty (present=false) elsewhere.
  eh_function_ir_t eh_ir;
  // Libc++ idiom evidence (SSO string dtor sites) built once at
  // module load time for modules with a "free" import/export. Empty elsewhere.
  libcxx_function_ir_t libcxx_ir;
};

struct export_t
{
  std::string name;
  uint32 index = 0;
};

struct local_name_entry_t
{
  uint32 local_index = 0;
  std::string name;
};

struct function_local_names_t
{
  uint32 function_index = 0;
  std::vector<local_name_entry_t> locals;
};

struct function_import_t
{
  std::string module;
  std::string name;
};

struct table_slot_t
{
  uint32 table_index = 0;
  uint32 slot_index = 0;
  uint32 function_index = 0;
  uint32 function_type_index = 0;
  uint32 segment_order = 0;
};

struct global_t
{
  uchar type = 0;
  uchar mutability = 0;
  uint64 initial_bits = 0;
  uint32 storage_offset = 0;
  bool imported = false;
  std::string module_name;
  std::string field_name;
  std::string import_helper;
};

struct element_offset_value_t
{
  uint64_t value = 0;
  bool dynamic = false;
};

// View of a captured DWARF custom section. The bytes live in the
// module buffer (embedded) or in owned external-file buffers.
struct dwarf_section_ref_t
{
  const uchar *data = nullptr;
  size_t size = 0;
};

struct dwarf_sections_t
{
  dwarf_section_ref_t abbrev;
  dwarf_section_ref_t info;
  dwarf_section_ref_t str;
  dwarf_section_ref_t str_offsets;
  dwarf_section_ref_t addr;
  std::vector<std::string> external_paths;
  bool has_debug_info() const { return info.data != nullptr && abbrev.data != nullptr; }
};

// Best-effort DWARF name recovery: fills function_names
// local_names and data_segment_names using fill-empty-first policy, so a
// module without DWARF keeps byte-identical naming. Never fails the load.
bool dwarf_apply_names(struct module_t *module, const char *input_file_path);

struct module_t
{
  std::vector<wasm_type_t> types;
  std::vector<uint32> imported_func_types;
  std::vector<uint32> tag_type_indices;
  std::vector<uchar> imported_tags;
  std::vector<function_import_t> function_imports;
  std::vector<uint32> defined_func_types;
  std::vector<body_t> bodies;
  std::vector<export_t> function_exports;
  std::vector<export_t> global_exports;
  std::vector<export_t> function_names;
  std::vector<export_t> global_names;
  std::vector<export_t> data_segment_names;
  std::vector<function_local_names_t> local_names;
  dwarf_sections_t dwarf;
  struct elem_entry_t
  {
    uint32 slot_index = 0;
    uint32 function_index = 0;
  };
  std::vector<elem_entry_t> elem_entries;
  reader_t start_function_body = { nullptr, 0, 0 };
  std::vector<uint32> declared_function_indices;
  std::vector<uchar> table_types;
  std::vector<uchar> table_imported;
  std::vector<uchar> table_exported;
  std::vector<uchar> table_shared;
  std::vector<uchar> table64;
  std::vector<uchar> table_dynamic_elements;
  std::vector<uint64> table_minimums;
  std::vector<uint64> table_maximums;
  std::vector<uchar> table_has_maximum;
  std::vector<table_slot_t> table_slots;
  std::vector<uint64> data_segment_sizes;
  std::vector<int64> data_segment_offsets;
  // The producers section says the Go toolchain built this module.
  bool producers_go = false;
  go_profile_info_t go_profile;
  // Go function names recovered from a pclntab embedded in the data
  // section (go1.18+ wire format), indexed by DEFINED function index
  // (imports excluded). Empty when the module carries no pclntab.
  std::vector<std::string> pclntab_function_names;
  // Go type data layer — moduledata/types/ArgInfo, scanned right
  // after the pclntab pass (fail-closed; see core/toolchains/go/types.hpp).
  go_type_info_t go_types;
  // Active/passive payloads; data_segment_data[i] has data_segment_sizes[i]
  // bytes.
  std::vector<bytevec> data_segment_data;
  // For global.get-placed segments: the referenced global index; UINT32_MAX
  // otherwise (constant offsets live in data_segment_offsets, passive = -1).
  std::vector<uint32> data_segment_globals;
  std::vector<global_t> globals;
  size_t global_storage_size = 0;
  uint32 declared_data_count = 0;
  uint32 start_function_index = UINT32_MAX;
  bool has_data_count = false;
  bool uses_data_segment_ops = false;
  bool uses_i31 = false;
  // True when the import section names the gojs/go runtime module.
  bool is_go = false;
  // Byte-level Go provenance — the import signal AND the SP/CTXT/
  // g/RET/PAUSE global roles AND the majority PC_B signatures AND the
  // majority SP frame prologues all hold (see scan_go_provenance). Only
  // with this flag does the emulator trust cross-function dispatch jumps.
  bool go_provenance = false;
  // Per-function shape flags (defined functions, same order):
  // bit0 = (param i32)(result i32) PC_B signature,
  // bit1 = `global.get 0; i32.const N; i32.sub; global.set 0` prologue.
  std::vector<uchar> go_function_shapes;
  bool has_generic_eqref = false;
  bool has_externref = false;
  uchar failure_section_id = 0xFF;
  bytevec code;
  uint32 memory_count = 0;
  uint64 memory_minimum_pages = 0;
  uint64 memory_declared_maximum_pages = 0;
  bool memory64 = false;
  bool memory_shared = false;
  bool memory_imported = false;
  bool memory_has_declared_maximum = false;
  bool memory_exported = false;
  bool uses_memory_grow = false;
  bool fixed_memory32 = false;
  bool fixed_memory64 = false;
  uint64 fixed_memory_bytes = 0;
  bool growable_memory32 = false;
  bool growable_memory64 = false;
  uint64 growable_memory_pages = 0;
  wasmhex_bounds_mode_t bounds_mode = wasmhex_bounds_mode_t::hidden;
  wasmhex_brtable_mode_t brtable_mode = wasmhex_brtable_mode_t::chain;
  std::string c_stack_global_override;
  uint32 c_stack_global_index = UINT32_MAX;
  bool c_stack_global_resolved = false;
  const char *failure_reason = nullptr;
  // Fuzz-found: reject() stores the raw pointer, so callers
  // passing a temporary std::string's c_str() (module.cpp resolve_c_stack_
  // global) left a dangling failure_reason. Owning storage closes the
  // whole class; literals copy cheaply.
  std::string failure_reason_storage;
  // Structured rejection info, filled by validate_module().
  diagnostic_t diagnostic;
  // Structured rejection at the source. Every reject path in the core
  // calls this with the error code riding along; the reason string is
  // display-only (failure_reason / diagnostic.detail). First rejection
  // wins — later calls are ignored.
  void reject(error_code code, const char *reason);
  // Typed-validation diagnostics (core/function_validator.cpp).
  uint32 failure_function_index = UINT32_MAX;
  size_t failure_body_offset = 0;
  uint32 functions_not_fully_validated = 0;
  const char *first_validation_gap = nullptr;
  // Indices of functions whose validation stopped at an unmodeled
  // opcode family (state=unsupported). Such functions are NOT validated —
  // downstream consumers must not treat them as checked.
  std::vector<uint32> unsupported_function_indices;

  bool fixed_memory() const { return fixed_memory32 || fixed_memory64; }
  bool has_memory_bounds_check() const
  {
    return bounds_mode != wasmhex_bounds_mode_t::hidden
      && (fixed_memory() || growable_memory32 || growable_memory64);
  }
  bool resolve_c_stack_global();

  bool parse(const uchar *data, size_t size);
  bool parse_custom(reader_t *r);
  bool parse_imports(reader_t *r);
  bool parse_exports(reader_t *r);
  bool parse_types(reader_t *r);
  bool parse_tags(reader_t *r);
  bool parse_data(reader_t *r);
  // Scan the active data segments for a Go pclntab (magic 0xFFFFFFF1
  // go1.20+, 0xFFFFFFF0 go1.18/19) and recover function names.
  bool parse_pclntab();
  // Retain the shape-validated pclntab header + per-function _func
  // addresses (no name reads) for the type data layer.
  void retain_go_pclntab();
  bool parse_data_count(reader_t *r);
  bool parse_start(reader_t *r);
  bool parse_elements(reader_t *r);
  bool parse_globals(reader_t *r);
  bool resolve_element_slots();
  bool function_type_index(uint64 function_index, uint32 *type_index);
  // Byte-level Go provenance scan (see parse_code.cpp).
  void scan_go_provenance();
  void resolve_go_profile(); // Classify the toolchain version
  // Byte-exact scan of the go1.26 call-site protocol (see body_t::
  // go_calls) for every function whose provenance flags carry the
  // dispatch bit.
  void scan_go_calls();
  const table_slot_t *find_table_slot(uint32 table_index, uint32 slot_index) const;
  bool function_types_compatible(uint32 expected_type, uint32 actual_type) const;
  bool static_dynamic_call_table(uint32 table_index, uint32 type_index, uint64 *table_size) const;
  bool static_known_call_table(uint32 table_index, uint32 *table_size) const;
  bool read_value_type(reader_t *r, uchar *type, int64 *heap_type = nullptr,
                       uchar *ref_form = nullptr);
  bool validate_heap_type_index(int64 heap_type);
  bool skip_supported_instruction(reader_t *r, uchar opcode, bool memory64, size_t memory_count);
  bool parse_functions(reader_t *r);
  bool parse_tables(reader_t *r);
  bool parse_memories(reader_t *r);
  bool parse_code(reader_t *r);
  bool scan_control(body_t *body, const uchar *expression, size_t size,
                    const wasm_type_t &function_type);
  bool skip_limits(reader_t *r, uint64 *minimum = nullptr, uint64 *flags_out = nullptr,
                   uint64 *maximum = nullptr, bool *has_maximum = nullptr);
  bool parse_memory_type(reader_t *r, bool imported);
  uint16 seen_sections = 0;
};

struct ref_fact_t
{
  std::vector<uint32> targets;
  bool known = false;
};

struct control_frame_t
{
  uchar kind;
  uint32 start;
  uint32 body;
  uint32 otherwise = UINT32_MAX;
  uint32 end = UINT32_MAX;
  std::vector<uchar> param_types;
  std::vector<uchar> result_types;
  std::vector<uchar> label_types;
  std::vector<int64> param_heap_types;
  std::vector<int64> result_heap_types;
  std::vector<int64> label_heap_types;
  std::vector<uchar> param_ref_forms;
  std::vector<uchar> result_ref_forms;
  std::vector<uchar> label_ref_forms;
  std::vector<ref_fact_t> ref_local_before;
  std::vector<ref_fact_t> ref_local_then;
  size_t eh_try_index = SIZE_MAX;
  bool has_else = false;
  bool in_catch = false;
};

struct pending_branch_t
{
  size_t control_index;
  std::vector<size_t> target_frames;
};

// Shared pure helpers (defined in core/module.cpp)
bool skip_sleb(reader_t *r, unsigned bits);

// printf-style formatting into std::string (shared helper).
std::string format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void append_uleb(bytevec *out, uint64 value);

size_t uleb_size(uint64 value);

uint8_t section_order(uchar id);

const char *c_type(uchar wasm_type);

bool valid_identifier(const std::string &name);

std::string sanitize_identifier(const std::string &raw);

// `pkg.__T_.m` (the Go wasm mangling of `pkg.(*T).m`) ->
// `pkg.(*T).m`; every other name passes through unchanged. Presentation
// only — the mangled pclntab/export name stays the module ground truth.
std::string go_method_display_name(const std::string &raw);

std::string imported_global_helper_name(uint32 index, const std::string &module,
                                        const std::string &field);

std::string imported_global_name(uint32 index, const std::string &module, const std::string &field,
                                 std::unordered_set<std::string> *used_names);

std::string function_name(uint32 index, const std::vector<export_t> &exports,
                          const std::vector<export_t> &names,
                          std::unordered_set<std::string> *used_names = nullptr,
                          const std::string *pclntab_name = nullptr);

std::string imported_function_name(uint32 index, const std::string &module,
                                   const std::string &field, const module_t &mod,
                                   std::unordered_set<std::string> *used_names = nullptr);

void resolve_got_globals(module_t &module, std::unordered_map<uint32, std::string> *got_names);

} // namespace wasmhex::loader

// Bridge for the processor/decompiler modules (namespace wasmhex): they
// historically referenced these names unqualified through the old shared
// headers; re-expose them from the pure core.
namespace wasmhex
{
using loader::wasmhex_bounds_mode_t;
using loader::wasmhex_brtable_mode_t;
using loader::wasmhex_call_trap_t;
using loader::wasmhex_sparse_dispatch_t;
using loader::wasmhex_c_frame_metadata_t;
using loader::WASMHEX_MAX_SPARSE_TABLE_DISPATCH_CASES;
namespace wasmhex_vt = loader::wasmhex_vt;
using loader::wasmhex_vt::vt_ok;
using loader::wasmhex_vt::vt_truncated;
using loader::wasmhex_vt::vt_unsupported_heap;
using loader::wasmhex_vt::value_type_t;
using loader::wasmhex_vt::value_type_status_t;
using loader::wasmhex_vt::ref_kind_t;
using loader::wasmhex_vt::decode_value_type;
using loader::read_sleb33; // moved to binary/leb128.hpp
using loader::wasmhex_vt::supported_abstract_heap_type;
using loader::wasmhex_vt::heap_type_none;
using loader::wasmhex_vt::heap_func;
using loader::wasmhex_vt::heap_extern;
using loader::wasmhex_vt::heap_eq;
using loader::wasmhex_vt::heap_i31;
using loader::wasmhex_vt::heap_struct;
using loader::wasmhex_vt::heap_array;
} // namespace wasmhex

// Global-namespace bridge: the processor/decompiler sources historically
// used these names unqualified from the old global-namespace headers.
using wasmhex::wasmhex_bounds_mode_t;
using wasmhex::wasmhex_brtable_mode_t;
using wasmhex::wasmhex_call_trap_t;
using wasmhex::wasmhex_sparse_dispatch_t;
using wasmhex::wasmhex_c_frame_metadata_t;
