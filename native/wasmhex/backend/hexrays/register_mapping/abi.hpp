#pragma once
#include <typeinf.hpp>
#include "frame.hpp"

namespace wasmhex
{
constexpr int multi_return_registers[] = { 0, 2, 1, 3, 6, 7 };
bool get_wasm_argument_register(size_t word_index, int *reg);
bool wasm_argument_on_stack(size_t word_index, int size);
size_t wasm_argument_word_count(size_t word_index, int size);
bool make_wasm_argument_location(size_t word_index, int size, argloc_t *location);
bool append_wasm_user_argument(qstring *declaration, const char *type, const char *name,
                               size_t word_index, int size, bool comma);

// Shared multi-result ABI helpers: single definitions linked into both
// the loader and the processor module so the register/stack word accounting
// for i64/f64 values at register boundaries cannot drift between the two.
// Note: the default value for allow_sret lives in the pre-existing
// declaration in processor/internal.hpp (which includes this header); do not
// repeat it here or C++ rejects the redefinition of the default argument.
// Callers in the loader pass allow_sret explicitly.
int wasm_type_size(uchar type);
qstring tuple_type_name(const std::vector<uchar> &results);
bool multi_result_word_count(const std::vector<uchar> &results, size_t *words, bool allow_sret);
bool get_multi_result_type(const std::vector<uchar> &results, tinfo_t *tuple_type);
bool tuple_layout_matches(const tinfo_t &tuple_type, const std::vector<uchar> &results);
bool get_multi_result_type(const std::vector<uchar> &results, tinfo_t *tuple_type);
bool make_multi_return_location(const std::vector<uchar> &results, argloc_t *return_location);

// Bridge overloads: the loader's shared helpers use std::vector, while the
// processor module's metadata code passes qvector. Forward via conversion.
inline bool make_multi_return_location(const qvector<uchar> &results, argloc_t *return_location)
{
  return make_multi_return_location(std::vector<uchar>(results.begin(), results.end()),
                                    return_location);
}

inline bool multi_result_word_count(const qvector<uchar> &results, size_t *words, bool allow_sret)
{
  return multi_result_word_count(std::vector<uchar>(results.begin(), results.end()), words,
                                 allow_sret);
}

inline bool get_multi_result_type(const qvector<uchar> &results, tinfo_t *tuple_type)
{
  return get_multi_result_type(std::vector<uchar>(results.begin(), results.end()), tuple_type);
}
}
