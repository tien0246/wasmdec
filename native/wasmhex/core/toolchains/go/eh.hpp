// Emscripten EH evidence: invoke sites + the __THREW__ protocol + landing
// pads, proven once at module load time on the instruction stream; the
// ctree rewriter consumes blob 'i' marks only.
#pragma once

#include <cstdint>
#include <vector>

namespace wasmhex::loader
{

constexpr std::size_t kEhMaxInvokeSites = 4096u; // per body proven sites cap
                                                 // (duckdb wasm_func bodies reach 557+
                                                 // invoke calls — 512 dropped their tails)
constexpr std::size_t kEhMaxPads = 512u;         // per body proven pads cap

struct eh_invoke_site_ir_t
{
  std::uint32_t call_offset = UINT32_MAX;       // the call itself
  std::uint32_t threw_global = UINT32_MAX;      // global.get G (GOT.mem.__THREW__)
  std::uint32_t tee_local = UINT32_MAX;         // local.tee T (the flag pointer)
  std::uint32_t tee_offset = UINT32_MAX;        // global.get G offset (evidence)
  std::uint32_t zero_store_before = UINT32_MAX; // i32.store [T+0] = 0 before
  std::uint32_t read_offset = UINT32_MAX;       // i32.load [T+0] after the call
  std::uint32_t zero_store_after = UINT32_MAX;  // the reset store after the read
  std::uint32_t guard_br_offset = UINT32_MAX;   // br/br_if to the pad (optional)
  std::uint32_t pad_offset = UINT32_MAX;        // pad entry offset (optional)
  std::uint32_t wrapped_invoke_fn = UINT32_MAX; // direct import call, or the
                                                // legalize-i64 wrapper's invoke

  bool invoke_site_valid(std::uint32_t body_size) const;
};

// One proven landing pad.
struct eh_pad_ir_t
{
  std::uint32_t pad_offset = UINT32_MAX;    // entry offset (catch stmt position)
  std::uint32_t catch_offset = UINT32_MAX;  // call __cxa_find_matching_catch_N
  std::uint32_t catch_fn = UINT32_MAX;      // import index of the catch callee
  std::uint32_t resume_offset = UINT32_MAX; // call __resumeException
  std::uint32_t trap_offset = UINT32_MAX;   // unreachable after resume
  std::uint32_t cleanup_count = 0;          // proven cleanup insns between
  bool is_catch_all = false;                // __cxa_find_matching_catch_2

  bool pad_valid(std::uint32_t body_size) const;
};

struct eh_function_ir_t
{
  bool present = false;
  std::vector<eh_invoke_site_ir_t> invokes; // sorted by call_offset
  std::vector<eh_pad_ir_t> pads;            // sorted by pad_offset
};

struct module_t;

// Build eh_function_ir_t for every body that calls an invoke_* import (or a
// bytecode-proven legalize-i64 wrapper around one). Fails closed per site.
void build_eh_function_irs(module_t &module);

// Blob 'i' v1 (per-function EA) serialize/deserialize.
void encode_eh_function_ir_blob(const eh_function_ir_t &ir, std::vector<std::uint32_t> *out);
bool decode_eh_function_ir_blob(const std::uint32_t *words, std::size_t word_count,
                                eh_function_ir_t *out);

// Name recognizers shared with the tests (verbatim semantics of the old
// ctree layer's helpers in decompiler/rewriter_eh.cpp).
bool is_invoke_helper_name(const char *name);
bool is_find_matching_catch_name(const char *name);
bool is_find_matching_catch_2_name(const char *name);
bool is_resume_exception_name(const char *name);
bool is_builtin_trap_name(const char *name);

// Proven bytecode recognizer for Emscripten legalize-i64 wrapper bodies
// around an imported invoke_* helper (moved verbatim from rewriter_eh.cpp —
// the loader owns callee identity now).
bool match_legalize_i64_wrapper_body(const std::uint8_t *data, std::size_t size,
                                     std::uint32_t *out_callee_func_idx);

} // namespace wasmhex::loader
