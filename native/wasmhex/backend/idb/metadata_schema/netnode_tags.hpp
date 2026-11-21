#pragma once
#include <cstdint>

namespace wasmhex
{
// Central registry of every netnode tag used by wasmhex; one enum value per
// (slot, meaning) pair, `uchar` so the constants drop into the netnode APIs.

enum class tag_t : std::uint8_t
{
  // module-level scalars (index 0)
  memory_mode = 'M',           // uchar: 0 none, 1 memory32, 2 memory64
  memory_shared = 'S',         // uchar: shared-memory flag
  bounds_mode = 'w',           // uchar: 0 hidden, 2 explicit
  data_count = 'd',            // uint32: declared data segment count
  memory_selector = 'Y',       // sel_t: linear-memory selector
  fixed_memory_bytes = 'm',    // uint64: exact memory extent (fixed modes)
  growable_memory_pages = 'n', // uint64: growable ceiling in pages
  data_span = 'L',             // u64[2]: initialized-data span (begin, end)
  go_module_flag = 'G',        // uchar: 1 when imports name gojs/go
  go_struct_registry = 'v',    // uint32: schema version of the registered Go
                               // til structs; the decompiler latches per-IDB
  rodata_const = 'q',          // string: "0"/"1" WASMHEX_RODATA_CONST
  c_stack_global = 'p',        // string: WASMHEX_C_STACK_GLOBAL
  // 'I' at index 0 was the interproc-pass latch ('d6_interproc_done');
  // the pass was removed (dead code, no caller) but old IDBs may
  // still carry the written value.

  // index-keyed (type/function/global/table/data indexes)
  type_params = 'A',          // uleb-packed uchar vector: type params
  type_results = 'B',         // uleb-packed uchar vector: type results
  type_param_refs = 'a',      // ref metadata for type params
  imported_name = 'H',        // string: imported function name
  imported_decl = 'I',        // string: imported function prototype decl
  import_origin = 'o',        // string: "module.field" origin
  function_entry = 'F',       // ea_t: function index -> entry EA
  declared_function = 'e',    // uchar: declared (elem segment) marker
  global_type = 'g',          // uchar: global value type
  global_mutability = 'h',    // uchar: global mutability
  global_address = 'j',       // ea_t: global storage address
  global_import_helper = 'k', // string: import helper name (ida/metadata)
  table_count = 'x',          // uint32: number of tables
  table_minimum = 'y',        // uint64: table minimum size
  table_flags = 'z',          // uchar: table limit flags (index = table index;
                              // NOTE: occupies sup slot ('z', 0) for table 0)
  elem_function = 'f',        // uint32: elem slot -> function index
  wasmhex_index_hint = 'i',   // reserved

  // ea-keyed control-flow / metadata
  branch_targets = 'C',        // ea_t[]: control targets (also case chains)
  packed_branch_targets = 'c', // blob: large br_table metadata
  branch_depths = 'D',         // uint32[]: br_table depths
  branch_stub_list = 'E',      // ea_t[]: virtual stub EAs
  branch_edge_source = 'S',    // ea_t: stub -> br_table EA (collision with
                               // memory_shared above is fine: different slot)
  scope_end = 'X',             // ea_t: structured scope end EA
  function_end = 'W',          // uchar: function-end marker
  startup_helper = 'R',        // string: startup helper mnemonic
  unwind_catch = 'Y',          // ea_t: call/throw -> catch EA (EH)
  eh_catch = 'H',              // ea_t: try -> catch EA
  eh_payload_type = 'N',       // uchar: try payload value type
  eh_tag_index = 'O',          // uint32: try tag index
  eh_try_ea = 's',             // ea_t: c-frame try marker (loader)
  call_trap = 'Z',             // wasmhex_call_trap_t: trapping indirect call
  checked_call = 'K',          // struct: checked dynamic indirect call
  resolved_call_target = 'G',  // uint32: statically resolved call target
  sparse_dispatch = 'J',       // struct: sparse dispatch header
  dispatch_case_count = 'Q',   // uint32: dispatch case count
  call_continuation = 'C',     // ea_t: call continuation (slot sharing
                               // with branch_targets by design)
  memory_trap_edge = 'T',      // ea_t: check -> trap EA
  memory_check_edge = 'B',     // ea_t: access -> check EA
  dref_target = '8',           // ea_t: loader-proven constant-address dref
  dref_kind = '9',             // uchar: 1 memory read, 2 memory write,
                               // 3 global.get (ea-keyed, pairs with
                               // dref_target)
  param_pointer_masks = '4',   // blob at index 0: {u32 magic, u32 count,
  pointer_arg_records = '3',   // blob (ea-keyed, one per CALL instruction):
                               // {u32 magic, u8 count, then count x
                               // {u8 param_index, u8[3] pad, u64 address}} —
                               // the loader-proven constant arguments at
                               // pointer positions of that call (the
                               // constant may reach the call through a
                               // local, which the decompiler-side operand
                               // no longer carries as a constant). The
                               // decompiler swap consumes exactly these.
};

// The wire characters must stay stable across releases (existing IDBs).
static_assert(static_cast<int>(tag_t::memory_mode) == 'M', "wire stability");
static_assert(static_cast<int>(tag_t::go_module_flag) == 'G', "wire stability");

} // namespace wasmhex
