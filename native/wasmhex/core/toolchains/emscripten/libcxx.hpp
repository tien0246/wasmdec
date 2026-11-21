// libc++ idiom evidence: the SSO string destructor shape proven once at
// module load time on the instruction stream; consumers read blob 'l'.
#pragma once

#include <cstdint>
#include <vector>

namespace wasmhex::loader
{

constexpr std::size_t kLibcxxMaxSsoDtors = 1024u; // per body proven sites cap

enum class libcxx_sso_form_t : std::uint8_t
{
  pointer_tag8 = 1,  // load8_s [p+11], data load [p+0]
  pointer_tag32 = 2, // i32.load [p+8], data load [p+0]
  stack_tag8 = 3,    // SP-relative slots, delta 11
  stack_tag32 = 4,   // SP-relative slots, delta 8
};

// One proven std::string::~string site.
struct libcxx_sso_dtor_ir_t
{
  std::uint32_t guard_offset = UINT32_MAX;     // the `if` of the tag guard
  std::uint32_t tag_load_offset = UINT32_MAX;  // the signed tag read
  std::uint32_t free_call_offset = UINT32_MAX; // call <free> in the branch
  std::uint32_t free_fn = UINT32_MAX;          // the proven free callee index
  std::uint8_t form = 0;                       // libcxx_sso_form_t (raw)

  bool valid(std::uint32_t body_size) const;
};

struct libcxx_function_ir_t
{
  bool present = false;
  std::vector<libcxx_sso_dtor_ir_t> sso_dtors; // sorted by guard_offset
};

struct module_t;

// Build libcxx_function_ir_t for every body with a proven SSO dtor site.
// Fails closed per site. No-ops on modules without a "free" import/export.
void build_libcxx_function_irs(module_t &module);

// Blob 'b' v1 (per-function EA; 'l' is taken by the loader's local reference metadata) serialize/deserialize.
void encode_libcxx_function_ir_blob(const libcxx_function_ir_t &ir,
                                    std::vector<std::uint32_t> *out);
bool decode_libcxx_function_ir_blob(const std::uint32_t *words, std::size_t word_count,
                                    libcxx_function_ir_t *out);

// Name recognizer shared with the tests (verbatim semantics of the ctree
// layer's is_free_callee_name in decompiler/rewriter_libcxx.cpp).
bool is_free_callee_name(const char *name);

} // namespace wasmhex::loader
