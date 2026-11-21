#pragma once
#include <cstddef>

#include <netnode.hpp>
#include <typeinf.hpp>

#include "../../../core/ir/source_location.hpp"
#include "../../../core/toolchains/go/index.hpp"
#include "../../../core/toolchains/go/ir.hpp"
#include "../../../core/toolchains/go/types.hpp"

namespace wasmhex
{

// 'C': control targets (br/br_if/br_table stub chains)
// Keys are control EAs; the same wire char also carries call-continuation
// singles written by the dispatch path.
bool store_branch_targets(netnode &node, DatabaseEA key, const ea_t *targets, size_t count);
bool load_branch_targets(DatabaseEA key, qvector<ea_t> *targets);

// 'c': packed large br_table (depths + targets, no supval cap)
// depth_count < target_count is zero-padded (the scanner may return a short
// depth list); the blob always carries exactly one depth per target.
bool store_packed_branch_table(netnode &node, DatabaseEA key, const uint32_t *depths,
                               size_t depth_count, const ea_t *targets, size_t target_count);
bool load_packed_branch_table(DatabaseEA key, qvector<ea_t> *targets, qvector<uint32_t> *depths);

// 'D': br_table depths (small-table path)
bool store_branch_depths(netnode &node, DatabaseEA key, const uint32_t *depths, size_t count);
bool load_branch_depths(DatabaseEA key, qvector<uint32_t> *depths);

// 'T': structured-scope / branch value types
bool store_control_types(netnode &node, DatabaseEA key, const uchar *types, size_t count);
bool load_control_types(DatabaseEA key, qvector<uchar> *types);

// 'S': virtual-stub -> br_table source EA
bool store_edge_source(netnode &node, DatabaseEA key, ea_t source);
bool load_edge_source(DatabaseEA key, ea_t *source);

bool store_dispatch_cases(netnode &node, DatabaseEA key, const uint32 *cases, size_t count);

bool read_dispatch_cases(netnode &wasm_node, ea_t key, qvector<uint32> *cases);

bool store_go_module(netnode &node);

bool load_go_module();

// Named Go prototypes (Go type names via itab / known-API / descriptor
// anchors). Definitions in ida/metadata.cpp:
// make_parsed_wasm_function_type with a NAMED sret pointee.
bool make_parsed_wasm_function_type_ex(const qvector<uchar> &params, const qvector<uchar> &results,
                                       bool indirect, tinfo_t *out, bool force_sret,
                                       const char *sret_pointee);

// Materialize a virtual Go struct (go__string, go__js__Value, ...) in the
// til from the decoded type-descriptor layout. Replaces any prior definition
// with the same name (NTF_REPLACE).
bool ensure_go_struct_type(const char *name, const std::vector<loader::go_c_field_t> &fields,
                           uint64 total_size);

bool store_go_function_ir(netnode &node, DatabaseEA key, const loader::go_function_ir_t &ir);
bool load_go_function_ir(netnode &node, ea_t key, loader::go_function_ir_t *ir,
                         bool *has_blob = nullptr);

// ModuleIndex (decode-once blob 't' + 'Z' + 'F' cache)
// Returns the per-process index for THIS database's module blobs (identity
// checked on every call — see ida/metadata.cpp), or nullptr when the module
// netnode is absent. The returned pointer stays valid until the next call
// that changes the identity (re-fetch per reset_state, never stored across
// functions) or clear_wasmhex_module_index (closebase).
const loader::go_module_index_t *go_module_index(netnode &wasm_node);
void clear_wasmhex_module_index();
unsigned go_module_index_builds(); // process-cumulative rebuild count

loader::function_ir_t *go_function_ir(netnode &wasm_node, ea_t entry);
unsigned go_function_ir_builds();
void clear_wasmhex_function_ir_cache();

enum class wasmhex_cfg_reject_t : uchar
{
  out_of_range = 1,   // target > UINT32_MAX
  cross_function = 2, // cross-function target without Go dispatch provenance
  not_boundary = 3,   // not an instruction boundary of the shared linear decode
  decode_failed = 4,  // the boundary walk of the owning function failed
};
bool store_cfg_reject(ea_t func_ea, wasmhex_cfg_reject_t reason, ea_t from, ea_t to);
const char *cfg_reject_reason(wasmhex_cfg_reject_t reason);

struct go_xf_wire_t
{
  uchar version;     // 1
  uchar slot_count;  // proven result slot count
  ushort reserved;   // zero
  uint32 min_offset; // lowest proven result slot (window coordinates)
  uint32 extent;     // one past the highest result byte
  char c_name[48];   // NUL-terminated named composite C name ("go__string")
};
static_assert(sizeof(go_xf_wire_t) == 60, "wire stability");
constexpr uchar kGoXfWireVersion = 1;

} // namespace wasmhex
