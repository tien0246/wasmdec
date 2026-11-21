#pragma once
// Itype metadata single source (included at file scope, outside namespaces)
#include "../../core/binary/instructions.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <idaidp.hpp>
#include <hexrays.hpp>
#include <netnode.hpp>
#include <nalt.hpp>
#include <kernwin.hpp>
#include <funcs.hpp>
#include <bytes.hpp>
#include <diskio.hpp>
#include <loader.hpp>

#include "../../backend/hexrays/ctree_presentation/ctree_float.hpp"
#include "../../core/validation/families/atomic_validator.hpp"
#include "../../backend/idb/metadata_schema/call_metadata.hpp"
#include "../../backend/hexrays/register_mapping/frame.hpp"
#include "../../core/types.hpp"
#include "../../backend/hexrays/register_mapping/abi.hpp"
#include "../../backend/hexrays/host_adapter/host.hpp"

namespace wasmhex
{
using wasmhex::load_wasmhex_hexrays_host;

using wasmhex::get_wasm_argument_register;

using wasmhex::wasm_argument_on_stack;

using wasmhex::wasm_argument_word_count;

using wasmhex::make_wasm_argument_location;

using wasmhex::append_wasm_user_argument;

constexpr int wasmhex_proc_id = 0x8000;

using wasmhex::multi_return_registers;

struct checked_table_call_t
{
  uint32 table_index;
  uint32 type_index;
  uint64 table_size;
  uchar index_type;
};

constexpr char wasmhex_node_name[] = "$ wasmhex";

bool valid_fixed_memory_extent(uchar memory_mode, uint64 memory_bytes);

bool get_wasm_memory_extent(netnode &wasm_node, uchar memory_mode, uint64 *memory_bytes,
                            bool *growable = nullptr);

bool fixed_memory_extent_fits_ida(uchar memory_mode, uint64 memory_bytes);

bool get_wasm_bounds_mode(netnode &wasm_node, wasmhex_bounds_mode_t *mode);

bool make_wasm_memory_selector(mop_t *selector, ea_t ea,
                               wasmhex_bounds_mode_t bounds_mode
                               = wasmhex_bounds_mode_t::explicit_bounds,
                               bool constant_address = false);

enum class wasm_table_metadata_status_t
{
  ok,
  missing,
  out_of_range,
  malformed,
};

wasm_table_metadata_status_t get_wasm_table_metadata(netnode &wasm_node, uint64 table_index,
                                                     uint32 *table_count, uint64 *minimum,
                                                     uchar *flags);

enum wasmhex_itype_t
{
// clang-format off: clang-format 21 (CI) and 23 (dev) disagree on the
// post-macro-entry indent here; keep the 2-space enum-body style.
#define WASMHEX_GEN_ENUM(name, byte, sub, imm, mnem, flags) wasmhex_##name,
  WASMHEX_ITYPES(WASMHEX_GEN_ENUM)
#undef WASMHEX_GEN_ENUM

  wasmhex_last,
  // clang-format on
};

inline const instruc_t instructions[] = {
#define WASMHEX_GEN_INSN(name, byte, sub, imm, mnem, flags) { mnem, flags },
  WASMHEX_ITYPES(WASMHEX_GEN_INSN)
#undef WASMHEX_GEN_INSN
};

CASSERT(qnumber(instructions) == wasmhex_last);

// Byte -> itype/imm-kind lookup generated from the same single-source
// table. Prefix opcodes (0xFB/0xFC/0xFD/0xFE) map to their IMM_*_SUB kind;
// the first row wins, so the 0x10 row resolves to `call` (the loader swaps
// it to call.unwind via 'Y' metadata) and the null sentinel row is skipped.
namespace opcodes_lookup
{
using namespace wasmhex::opcodes;
struct row_t
{
  int byte;
  int imm;
  wasmhex_itype_t itype;
};
#define WASMHEX_GEN_ROW(name, byte, sub, imm, mnem, flags) { byte, int(imm), wasmhex_##name },
inline constexpr row_t rows[] = { WASMHEX_ITYPES(WASMHEX_GEN_ROW) };
#undef WASMHEX_GEN_ROW

struct info_t
{
  wasmhex_itype_t itype;
  opcodes::imm_kind_t imm;
};

inline constexpr info_t info(int byte)
{
  for ( const row_t &row : rows )
    if ( row.byte == byte && row.byte != 0 )
      return { row.itype, opcodes::imm_kind_t(row.imm) };
  return { wasmhex_null, opcodes::IMM_SPECIAL };
}
} // namespace opcodes_lookup

struct memory_instruction_t
{
  uchar type;
  uchar width;
  bool store;
  bool signed_load;
};

inline const memory_instruction_t memory_instructions[] = {
  { 0x7F, 4, false, false }, { 0x7E, 8, false, false }, { 0x7D, 4, false, false },
  { 0x7C, 8, false, false }, { 0x7F, 1, false, true },  { 0x7F, 1, false, false },
  { 0x7F, 2, false, true },  { 0x7F, 2, false, false }, { 0x7E, 1, false, true },
  { 0x7E, 1, false, false }, { 0x7E, 2, false, true },  { 0x7E, 2, false, false },
  { 0x7E, 4, false, true },  { 0x7E, 4, false, false }, { 0x7F, 4, true, false },
  { 0x7E, 8, true, false },  { 0x7D, 4, true, false },  { 0x7C, 8, true, false },
  { 0x7F, 1, true, false },  { 0x7F, 2, true, false },  { 0x7E, 1, true, false },
  { 0x7E, 2, true, false },  { 0x7E, 4, true, false },
};

CASSERT(qnumber(memory_instructions) == wasmhex_i64_store32 - wasmhex_i32_load + 1);

// Phase 2 branding: the processor short name is wasmdec, matching
// procs/wasmdec.dylib (OUTPUT_NAME) and the loader's *processor value —
// the -pwasmdec flag, the dylib file name and this name must all agree and
// stay distinct from IDA's built-in "wasm".
inline const char *const short_names[] = { "wasmdec", nullptr };

inline const char *const long_names[] = { "wasmdec decompiler", nullptr };

inline char register_name_storage[wasmhex_register_count][8];

inline const char *register_names[wasmhex_register_count];

struct register_names_init_t
{
  register_names_init_t()
  {
    for ( int i = 0; i < wasmhex_user_regs; ++i )
    {
      qsnprintf(register_name_storage[i], sizeof(register_name_storage[i]), "r%d", i);
      register_names[i] = register_name_storage[i];
    }
    qsnprintf(register_name_storage[wasmhex_cs_reg], sizeof(register_name_storage[0]), "cs");
    qsnprintf(register_name_storage[wasmhex_ds_reg], sizeof(register_name_storage[0]), "ds");
    register_names[wasmhex_cs_reg] = register_name_storage[wasmhex_cs_reg];
    register_names[wasmhex_ds_reg] = register_name_storage[wasmhex_ds_reg];
  }
};

inline register_names_init_t register_names_init;

inline const asm_t assembler = {
  ASH_HEXF3, 0,        "WebAssembly bytecode",
  0,         nullptr,  "org",
  "end",     ";",      '"',
  '\'',      "\\\"'",  ".ascii",
  ".byte",   ".2byte", ".4byte",
  ".8byte",
};

inline const asm_t *const assemblers[] = { &assembler, nullptr };

bool read_uleb(insn_t *insn, uint64 *value, unsigned bits = 32);

bool read_sleb(insn_t *insn, int64 *value, unsigned max_bytes = 5);

bool decode_insn_value_type(insn_t *insn, wasmhex::value_type_t *out);

struct wasm_value_t
{
  mop_t op;
  uchar type = 0;
  uint32 function_ref_index = UINT32_MAX;
  uint64 constant = 0;
  bool has_constant = false;
  bool owned_temp = false;
  bool is_c_frame_address = false;
  bool is_c_sp_base = false;
  int32 c_frame_offset = 0;
  bool is_table_base = false;
  uint32 table_base_offset = 0;
  // The 8-byte receiver-pointer value at a proven Go
  // pointer-receiver member access (the i32.wrap_i64 of the receiver word
  // load, plus its folded const offset). The member load/stx lowers
  // through the typed pointer instead of the flat linear address so the
  // engine's struct-offset pass can render `a1->field`.
  bool is_go_receiver_address = false;
};

struct wasm_local_t
{
  mop_t storage;
  uchar type = 0;
  tinfo_t tif;
  bool initialized = false;
  uint32 function_ref_index = UINT32_MAX;
};

struct wasm_control_frame_t
{
  uchar kind = 0;
  ea_t start = BADADDR;
  ea_t end = BADADDR;
  ea_t catch_handler = BADADDR;
  uchar catch_payload_type = 0x40;
  uint32 catch_tag_index = UINT32_MAX;
  qvector<wasm_value_t> base_stack;
  qvector<wasm_value_t> parameter_values;
  qvector<wasm_value_t> result_values;
  qvector<wasm_value_t> label_values;
  qvector<uchar> result_types;
  qvector<uchar> label_types;
  bool entered_reachable = true;
  bool in_else = false;
  bool in_catch = false;
};

struct wasm_branch_value_edge_t
{
  mop_t spill;
  mop_t merge;
  uchar type = 0;
  bool floating = false;
};

struct wasm_branch_edge_t
{
  ea_t source = BADADDR;
  uint32 index = 0;
  qvector<wasm_branch_value_edge_t> values;
  bool processed = false;
};

struct wasm_branch_resume_t
{
  ea_t target = BADADDR;
  qvector<wasm_value_t> stack;
};

struct wasm_indirect_dispatch_t
{
  ea_t source = BADADDR;
  ea_t continuation = BADADDR;
  uint32 table_size = 0;
  mop_t selector;
  bool tail_call = false;
  bool reference_call = false;
  qvector<wasm_value_t> base_stack;
  qvector<wasm_value_t> arguments;
  qvector<uchar> result_types;
  qvector<wasm_value_t> continuation_stack;
};

bool get_ea_list(ea_t key, uchar tag, qvector<ea_t> *values);

bool get_branch_table_metadata(ea_t key, qvector<ea_t> *targets, qvector<uint32> *depths);

bool get_u32_list(ea_t key, uchar tag, qvector<uint32> *values);

bool decode_type_vector(const uchar *data, size_t size, qvector<uchar> *types);

bool get_type_vector(netnode &wasm_node, uint32 index, uchar tag, qvector<uchar> *types);

bool get_type_vector(netnode &wasm_node, ea_t ea, uchar tag, qvector<uchar> *types);

bool decode_reference_metadata(const uchar *data, size_t size, size_t expected_count,
                               qvector<int64> *heap_types, qvector<uchar> *ref_forms);

bool get_reference_metadata(netnode &wasm_node, ea_t ea, uchar tag, size_t expected_count,
                            qvector<int64> *heap_types, qvector<uchar> *ref_forms);

int wasm_type_size(uchar type);

size_t wasm_results_size(const qvector<uchar> &types);

bool multi_result_word_count(const qvector<uchar> &types, size_t *words, bool allow_sret = false);

bool set_multi_return_regs(mcallinfo_t *callinfo, const qvector<uchar> &result_types);

bool get_multi_result_component(const mcallinfo_t &callinfo, size_t offset, int size, mop_t *out);

bool get_ea_value(ea_t key, uchar tag, ea_t *value);

bool get_dispatch_case_count(netnode &wasm_node, ea_t key, uint32 *case_count);

bool get_global_metadata(uint32 index, uchar *type, uchar *mutability, ea_t *address,
                         bool *imported, qstring *import_helper);

bool is_declared_function(uint32 index);

bool is_function_end(ea_t ea);

bool get_wasm_tinfo(uchar type, tinfo_t *tif);

uchar get_wasm_type(const tinfo_t &tif);

qstring tuple_type_name(const qvector<uchar> &results);

bool tuple_layout_matches(const tinfo_t &tuple_type, const qvector<uchar> &results);

bool get_multi_result_type(const qvector<uchar> &results, tinfo_t *tuple_type);

bool make_multi_return_location(const qvector<uchar> &results, argloc_t *return_location);

bool make_parsed_wasm_function_type(const qvector<uchar> &params, const qvector<uchar> &results,
                                    bool indirect, tinfo_t *out, bool force_sret = false);

// Forced-sret signature for caller-cross-checked Go
// multi results (metadata.cpp — the tuple display cannot be cast at the raw
// i32 call sites).
bool make_wasm_go_sret_function_type(const qvector<uchar> &params, const qvector<uchar> &results,
                                     tinfo_t *out);

bool make_wasm_function_type_uncached(const qvector<uchar> &params, const qvector<uchar> &results,
                                      bool indirect, tinfo_t *out);

inline std::map<std::string, tinfo_t> wasm_function_type_cache;

inline bool wasm_function_type_cache_scanned = false;

std::string make_wasm_function_type_key(const qvector<uchar> &params, const qvector<uchar> &results,
                                        bool indirect);

void scan_wasm_function_type_cache(netnode &wasm_node);

bool make_wasm_function_type(const qvector<uchar> &params, const qvector<uchar> &results,
                             bool indirect, tinfo_t *out);

void clear_wasm_function_type_cache();

bool make_regloc_mop(codegen_t &cdg, const argloc_t &loc, int size, mop_t *out);
void clear_wasmhex_filter_for_mba(mba_t *mba);
}
