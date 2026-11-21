// ModuleIndex: the immutable, decode-ONCE view of every module-level
// netnode record (blob 't' type layer, blob 'Z' table map, 'F' boundary).
#pragma once

#include "ir.hpp"
#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace wasmhex::loader
{

// Schema of the DERIVED structures (the addr index and the slot map — not
// the blob wire). Bump when the derivation changes so every in-memory
// index rebuilds; it is mixed into the identity below.
constexpr std::uint32_t kModuleIndexSchema = 1;

// The module-level index (one per IDB, built once per process per
// identity). All containers are value-owned — no pointer into IDB or
// netnode memory escapes, so an invalidated index can be dropped safely.
struct go_module_index_t
{
  std::uint64_t identity = 0; // go_module_identity over the source blobs
  go_type_info_t layer;       // blob 't' decoded ONCE (v1/v2 stream)
  bool layer_present = false; // blob 't' existed, sized and decoded
  // addr -> index into layer.descriptors (O(1); the layer's own
  // descriptor_by_addr stays a linear scan for the loader paths).
  std::unordered_map<std::uint64_t, std::size_t> descriptor_index;
  // table 0 slot -> module function index (blob 'Z', first entry wins —
  // the exact policy the per-F5 parse applied).
  std::unordered_map<std::uint32_t, std::uint32_t> table0_slot_to_fn;
  // Smallest live 'F' key (UINT32_MAX = probe failed / none exists).
  std::uint32_t import_count = UINT32_MAX;
  // Diagnostics: how often this cache served a build (1 per identity per
  // process is the goal; the IRTIME line prints the delta).
  std::uint64_t build_count = 0;

  const go_type_descriptor_t *find_descriptor(std::uint64_t addr) const;
};

// FNV-1a 64 (small inputs).
std::uint64_t go_fnv1a64(const std::uint8_t *bytes, std::size_t count,
                         std::uint64_t seed = 0xcbf29ce484222325ULL);
// Word-stride mix for the megabyte-scale blobs (see index.cpp).
std::uint64_t go_hash64(const std::uint8_t *bytes, std::size_t count, std::uint64_t seed);

// Identity over every ingredient the derivation reads. Lengths are mixed
// in so concatenated ingredients cannot alias.
std::uint64_t go_module_identity(const std::uint32_t *t_words, std::size_t t_word_count,
                                 const std::uint8_t *z_bytes, std::size_t z_byte_count,
                                 std::uint32_t import_count, std::uint32_t index_schema);

// Decode blob 't' into `out->layer` and derive the lookup structures.
// `t_words`/`t_word_count` = the raw blob bytes reinterpreted as u32
// words (nullptr/0 = no blob). The fail-closed gates are exactly the ones
// the three per-F5 decoders applied: a blob that is absent, not u32
// sized, or fails decode_go_type_info_blob leaves layer_present=false and
// every consumer bails the way it used to.
bool go_build_module_index(const std::uint32_t *t_words, std::size_t t_word_count,
                           const std::uint8_t *z_bytes, std::size_t z_byte_count,
                           std::uint32_t import_count, go_module_index_t *out);

// FunctionIR — the per-function decode-once
// cache. reset_state's collectors used to re-decode the whole body up to
// four times per F5 (shapes / notes / value / frame-legs) plus a fresh
// blob-'x' decode; they now read ONE linear instruction stream plus ONE
// decoded go_function_ir_t from here. Cache validity keys (all three must
// match or the entry rebuilds):
//   module_identity — from the ModuleIndex (module metadata changed)
//   body_hash       — word-stride hash over the raw body bytes
//                     [start_ea, end_ea) (user patched an instruction)
//   wire_hash       — over the raw blob-'x' bytes (metadata rewritten)
// The stream record carries exactly the fields every collector consumed
// {ea, itype, Op1.value}; per-consumer truncation happens at the use site
// so comparisons keep their original semantics.
struct go_insn_record_t
{
  std::uint64_t ea = 0;
  std::int32_t itype = 0;
  std::uint64_t op1 = 0;
};

struct function_ir_t
{
  std::uint64_t module_identity = 0;
  std::uint64_t body_hash = 0;
  std::uint64_t wire_hash = 0;
  bool wire_present = false; // a blob 'x' exists at the entry EA
  bool wire_valid = false;   // it decoded (load_go_function_ir semantics)
  bool stream_valid = false; // the linear body decode succeeded
  std::uint64_t start_ea = 0;
  std::uint64_t end_ea = 0;
  go_function_ir_t ir;                 // decoded blob 'x' (valid iff wire_valid)
  std::vector<go_insn_record_t> insns; // linear decode of [start_ea, end_ea)
  // 'j' memo: call-source EA -> (a dispatch blob exists) and its
  // decoded case table. The blobs are loader-written and immutable after
  // load; the memo lives with the function entry so a rebuilt entry
  // (bytes/metadata changed) starts clean.
  std::unordered_map<std::uint64_t, bool> dispatch_probe;
  std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> dispatch_cases;

  // Index of the record at `ea`, or SIZE_MAX when it is not an instruction
  // boundary of this stream (binary search — records are EA-sorted).
  std::size_t find(std::uint64_t ea) const;
};

} // namespace wasmhex::loader
