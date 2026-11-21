// LEB128 decoding through the core reader (uleb/sleb33/sleb64), including
// the edge cases the wasm spec cares about: truncation, overlong encodings,
#include "doctest.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../core/types.hpp"

using namespace wasmhex::loader;

namespace
{

// NOTE: the returned reader points INTO `bytes`; the vector must outlive it.
reader_t make_reader(const std::vector<uchar> &bytes)
{
  return reader_t{ bytes.data(), bytes.size(), 0 };
}

} // namespace

TEST_CASE("uleb32: canonical values")
{
  struct Case
  {
    std::vector<uchar> bytes;
    uint64 value;
  };
  const Case cases[] = {
    { { 0x00 }, 0 },
    { { 0x01 }, 1 },
    { { 0x7F }, 0x7F },
    { { 0x80, 0x01 }, 0x80 },
    { { 0x80, 0x80, 0x80, 0x80, 0x08 }, 0x80000000 }, // 2^31, still 5 bytes
  };
  for ( const Case &c : cases )
  {
    reader_t r = make_reader(c.bytes);
    uint64 value = 0;
    CHECK(r.uleb(&value));
    CHECK(value == c.value);
    CHECK(r.pos == c.bytes.size());
  }
}

TEST_CASE("uleb32: rejects overflow and truncation")
{
  // 2^32 does not fit uleb32 (reader enforces the 32-bit bound).
  const std::vector<uchar> bytes_r = { 0x80, 0x80, 0x80, 0x80, 0x10 };
  reader_t r = make_reader(bytes_r);
  uint64 value = 0;
  CHECK_FALSE(r.uleb(&value, 32));

  // Truncated continuation.
  const std::vector<uchar> bytes_t = { 0x80 };
  reader_t t = make_reader(bytes_t);
  CHECK_FALSE(t.uleb(&value));

  // Sixth byte of a 32-bit uleb is never reached: 5-byte limit.
  const std::vector<uchar> bytes_s = { 0x80, 0x80, 0x80, 0x80, 0x80, 0x00 };
  reader_t s = make_reader(bytes_s);
  CHECK_FALSE(s.uleb(&value, 32));
}

TEST_CASE("uleb64 round trip via append_uleb")
{
  const uint64 values[] = {
    0, 1, 0x7F, 0x80, 0x3FFF, 0x4000, 0xFFFFFFFF, 0x100000000ULL, 0xFFFFFFFFFFFFFFFFULL,
  };
  for ( uint64 value : values )
  {
    bytevec encoded;
    append_uleb(&encoded, value);
    reader_t r = make_reader(encoded);
    uint64 decoded = 0;
    REQUIRE(r.uleb(&decoded, 64));
    CHECK(decoded == value);
    CHECK(uleb_size(value) == encoded.size());
  }
}

TEST_CASE("sleb33: signed heap-type encodings")
{
  struct Case
  {
    std::vector<uchar> bytes;
    int64 value;
  };
  const Case cases[] = {
    { { 0x00 }, 0 },                      // type index 0
    { { 0x05 }, 5 },                      // type index 5
    { { 0x40 }, -64 }, { { 0x6A }, -22 }, // array  (0x6A = sleb(-0x16))
    { { 0x6B }, -21 },                    // struct
    { { 0x6C }, -20 },                    // i31
    { { 0x6D }, -19 },                    // eq
    { { 0x70 }, -16 },                    // func
    { { 0x6F }, -17 },                    // extern
    { { 0x7F }, -1 },
  };
  for ( const Case &c : cases )
  {
    reader_t r = make_reader(c.bytes);
    int64 value = 0;
    REQUIRE(r.sleb33(&value));
    CHECK(value == c.value);
  }
}

TEST_CASE("sleb33: rejects overlong/truncated encodings")
{
  uint64 value = 0;
  // Truncated continuation.
  const std::vector<uchar> bytes_t = { 0x80 };
  reader_t t = make_reader(bytes_t);
  CHECK_FALSE(t.sleb33(reinterpret_cast<int64 *>(&value)));
  // Sixth byte never appears for s33.
  const std::vector<uchar> bytes_s = { 0xFF, 0xFF, 0xFF, 0xFF, 0x7F };
  reader_t s = make_reader(bytes_s);
  CHECK(s.sleb33(reinterpret_cast<int64 *>(&value))); // last legal 5-byte form
  const std::vector<uchar> bytes_u = { 0x80, 0x80, 0x80, 0x80, 0x80, 0x00 };
  reader_t u = make_reader(bytes_u);
  CHECK_FALSE(u.sleb33(reinterpret_cast<int64 *>(&value)));
}

TEST_CASE("sleb64: extreme values")
{
  struct Case
  {
    std::vector<uchar> bytes;
    int64 value;
  };
  const Case cases[] = {
    { { 0x00 }, 0 },
    { { 0x7F }, -1 },
    { { 0x80, 0x7F }, -128 },
    { { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F }, -1 },
    // INT64_MIN as accepted by the production reader: the final byte's
    // low bit lands on bit 63 (the post-loop shift is already past 64, so
    // no further sign extension happens).
    { { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7F }, INT64_MIN },
  };
  for ( const Case &c : cases )
  {
    reader_t r = make_reader(c.bytes);
    int64 value = 0;
    REQUIRE(r.sleb64(&value));
    CHECK(value == c.value);
  }
}

TEST_CASE("reader: name() validates UTF-8 and bounds")
{
  std::string out;
  const std::vector<uchar> bytes_ok = { 3, 'a', 'b', 'c' };
  reader_t ok = make_reader(bytes_ok);
  REQUIRE(ok.name(&out));
  CHECK(out == "abc");

  const std::vector<uchar> bytes_bad_utf8 = { 1, 0xFF };
  reader_t bad_utf8 = make_reader(bytes_bad_utf8);
  CHECK_FALSE(bad_utf8.name(&out));

  const std::vector<uchar> bytes_overlong = { 5, 'a' };
  reader_t overlong = make_reader(bytes_overlong);
  CHECK_FALSE(overlong.name(&out));
}

#define WASMHEX_PURE_STRING_TEST
#include "../backend/hexrays/ctree_presentation/rewriter_idiom.cpp"
#include "../core/toolchains/go/eh.hpp"
#include "../backend/hexrays/ctree_presentation/rewriter_eh.cpp"
#include "../backend/hexrays/ctree_presentation/rewriter_libcxx.cpp"
#include "../ida/loader/options.cpp"
#undef WASMHEX_PURE_STRING_TEST

namespace
{

struct mock_qstring_t
{
  std::string s;
  static constexpr std::size_t npos = std::string::npos;

  mock_qstring_t() = default;
  explicit mock_qstring_t(const char *p) : s(p != nullptr ? p : "") {}
  mock_qstring_t(const char *p, std::size_t len) : s(p, len) {}

  bool empty() const { return s.empty(); }
  std::size_t size() const { return s.size(); }
  std::size_t length() const { return s.size(); }
  const char *c_str() const { return s.c_str(); }
  std::size_t find(char c, std::size_t pos = 0) const { return s.find(c, pos); }

  mock_qstring_t substr(std::size_t pos = 0, std::size_t end_idx = npos) const
  {
    const std::size_t endp = std::min(length(), end_idx);
    if ( pos >= endp )
      pos = endp;
    return mock_qstring_t(c_str() + pos, endp - pos);
  }

  bool operator<(const mock_qstring_t &o) const { return s < o.s; }
  bool operator==(const mock_qstring_t &o) const { return s == o.s; }
};

} // namespace

TEST_CASE("M1: options::detail::parse_debug_categories qstring end-index substr semantics")
{
  // IDA SDK `_qstring::substr(size_t pos, size_t n)` takes an END index (non-inclusive),
  // not a length like `std::string::substr`. Verify with a mock string implementing
  // IDA `_qstring::substr` end-index semantics that 4+ comma-separated categories parse
  // into 4 non-empty tokens.
  std::set<mock_qstring_t> tokens;
  wasmhex::options::detail::parse_debug_categories(mock_qstring_t("a,b,c,d"), &tokens);
  REQUIRE(tokens.size() == 4u);
  CHECK(tokens.count(mock_qstring_t("a")) == 1u);
  CHECK(tokens.count(mock_qstring_t("b")) == 1u);
  CHECK(tokens.count(mock_qstring_t("c")) == 1u);
  CHECK(tokens.count(mock_qstring_t("d")) == 1u);

  std::set<mock_qstring_t> real_categories;
  wasmhex::options::detail::parse_debug_categories(mock_qstring_t("phases,verify,g2,go"),
                                                   &real_categories);
  REQUIRE(real_categories.size() == 4u);
  CHECK(real_categories.count(mock_qstring_t("phases")) == 1u);
  CHECK(real_categories.count(mock_qstring_t("verify")) == 1u);
  CHECK(real_categories.count(mock_qstring_t("g2")) == 1u);
  CHECK(real_categories.count(mock_qstring_t("go")) == 1u);

  // Edge cases: empty string, single category, and empty tokens between commas.
  std::set<mock_qstring_t> empty_set;
  wasmhex::options::detail::parse_debug_categories(mock_qstring_t(""), &empty_set);
  CHECK(empty_set.empty());
  wasmhex::options::detail::parse_debug_categories(mock_qstring_t("phases"), &empty_set);
  REQUIRE(empty_set.size() == 1u);
  CHECK(empty_set.count(mock_qstring_t("phases")) == 1u);
}

TEST_CASE("M4-S4 C6.1: verify_unique_ptr_delete_site positive and near-miss negative")
{
  using wasmhex::libcxx_detail::unique_ptr_delete_candidate_t;
  using wasmhex::libcxx_detail::verify_unique_ptr_delete_site;

  // Positive: ExtractJoinRelations v122 `if (v122 != 0) __indirect_function_table[*(*v122 + 4)](v122)`
  unique_ptr_delete_candidate_t ok{};
  ok.has_null_guard = true;
  ok.has_else = false;
  ok.is_indirect_table_call = true;
  ok.index_reads_vtable_from_ptr = true;
  ok.vtable_byte_offset = 4;
  ok.arg_count = 1;
  ok.cond_lvar = 122;
  ok.vtable_obj_lvar = 122;
  ok.call_arg_lvar = 122;
  ok.inner_stmt_has_external_label = false;
  CHECK(verify_unique_ptr_delete_site(ok));

  // Near-miss 1: ExecuteTask v16/v17 calls vtable+8 (__shared_weak_count::__on_zero_shared), not +4.
  unique_ptr_delete_candidate_t bad_vtoff = ok;
  bad_vtoff.vtable_byte_offset = 8;
  CHECK_FALSE(verify_unique_ptr_delete_site(bad_vtoff));

  // Near-miss 2: ExtractJoinRelations LABEL_219 inside `if (v182 != 0)` jumped to from outside.
  unique_ptr_delete_candidate_t bad_label = ok;
  bad_label.inner_stmt_has_external_label = true;
  CHECK_FALSE(verify_unique_ptr_delete_site(bad_label));

  // Near-miss 3: unguarded vtable+4 call (no `if (p != 0)` wrapper).
  unique_ptr_delete_candidate_t bad_unguarded = ok;
  bad_unguarded.has_null_guard = false;
  CHECK_FALSE(verify_unique_ptr_delete_site(bad_unguarded));

  // Near-miss 4: mismatched `this` argument vs vtable object local.
  unique_ptr_delete_candidate_t bad_arg_var = ok;
  bad_arg_var.call_arg_lvar = 123;
  CHECK_FALSE(verify_unique_ptr_delete_site(bad_arg_var));

  // Near-miss 5: extra call argument or `else` branch.
  unique_ptr_delete_candidate_t bad_argc = ok;
  bad_argc.arg_count = 2;
  CHECK_FALSE(verify_unique_ptr_delete_site(bad_argc));
  unique_ptr_delete_candidate_t bad_else = ok;
  bad_else.has_else = true;
  CHECK_FALSE(verify_unique_ptr_delete_site(bad_else));
}

TEST_CASE("M4-S4 C6.2: verify_sso_string_dtor_site positive and near-miss negative")
{
  using wasmhex::libcxx_detail::sso_string_dtor_candidate_t;
  using wasmhex::libcxx_detail::verify_sso_string_dtor_site;

  // Positive 1: pointer form in ExecuteTask (`if (*(char *)(v37 + 11) < 0) free(*(void **)v37)`).
  sso_string_dtor_candidate_t ptr_ok{};
  ptr_ok.is_signed_lt_zero = true;
  ptr_ok.is_free_call = true;
  ptr_ok.free_arg_count = 1;
  ptr_ok.is_pointer_form = true;
  ptr_ok.tag_read_size = 1;
  ptr_ok.data_read_size = 4;
  ptr_ok.tag_byte_offset = 11;
  ptr_ok.data_byte_offset = 0;
  ptr_ok.same_base_expr = true;
  CHECK(verify_sso_string_dtor_site(ptr_ok));

  // Positive 2: stack 1-byte tag form in ExtractJoinRelations (`if (v768 < 0) free(v767)` at +11).
  sso_string_dtor_candidate_t stk1_ok{};
  stk1_ok.is_signed_lt_zero = true;
  stk1_ok.is_free_call = true;
  stk1_ok.free_arg_count = 1;
  stk1_ok.is_stack_form = true;
  stk1_ok.tag_read_size = 1;
  stk1_ok.data_read_size = 4;
  stk1_ok.data_stkoff = 0x238;
  stk1_ok.tag_stkoff = 0x243;
  CHECK(verify_sso_string_dtor_site(stk1_ok));

  // Positive 3: stack 4-byte MSB-sign form in ExtractJoinRelations (`if (v745 < 0) free(v743)` at +8).
  sso_string_dtor_candidate_t stk4_ok = stk1_ok;
  stk4_ok.tag_read_size = 4;
  stk4_ok.data_stkoff = 0xB8;
  stk4_ok.tag_stkoff = 0xC0;
  CHECK(verify_sso_string_dtor_site(stk4_ok));

  // Near-miss 1: wrong byte offset (+10 instead of +11).
  sso_string_dtor_candidate_t bad_off = ptr_ok;
  bad_off.tag_byte_offset = 10;
  CHECK_FALSE(verify_sso_string_dtor_site(bad_off));

  // Near-miss 2: different base pointer for tag read vs free argument.
  sso_string_dtor_candidate_t bad_base = ptr_ok;
  bad_base.same_base_expr = false;
  CHECK_FALSE(verify_sso_string_dtor_site(bad_base));

  // Near-miss 3: stack delta mismatch (e.g. +12 instead of +11 for 1-byte tag).
  sso_string_dtor_candidate_t bad_stk_delta = stk1_ok;
  bad_stk_delta.tag_stkoff = stk1_ok.data_stkoff + 12;
  CHECK_FALSE(verify_sso_string_dtor_site(bad_stk_delta));

  // Near-miss 4: has `else` branch or non-signed comparison.
  sso_string_dtor_candidate_t bad_else = ptr_ok;
  bad_else.has_else = true;
  CHECK_FALSE(verify_sso_string_dtor_site(bad_else));
}

TEST_CASE("M4-S4 C6.3: verify_vector_string_dtor_site positive and near-miss negative")
{
  using wasmhex::libcxx_detail::vector_string_dtor_candidate_t;
  using wasmhex::libcxx_detail::verify_vector_string_dtor_site;

  // Positive: ExtractJoinRelations v740/v741 and AddRelationTdom v14+56.
  vector_string_dtor_candidate_t ok{};
  ok.end_minus_begin_bytes = 4;
  ok.has_outer_nonnull_check = true;
  ok.has_begin_ne_end_check = true;
  ok.has_do_loop = true;
  ok.step_prev_bytes = -12;
  ok.tag_offset_from_it = -1;
  ok.tag_read_size = 1;
  ok.tag_is_signed_lt_zero = true;
  ok.calls_free_on_prev_deref = true;
  ok.iter_decrement_bytes = 12;
  ok.loop_cond_compares_prev_to_begin = true;
  ok.resets_end_to_begin = true;
  ok.calls_free_on_begin = true;
  ok.has_external_incoming_label = false;
  ok.temps_do_not_escape = true;
  CHECK(verify_vector_string_dtor_site(ok));

  // Near-miss 1: vector<RelationStats> dtor loop in ExtractJoinRelations (step -64, not -12).
  vector_string_dtor_candidate_t bad_step = ok;
  bad_step.step_prev_bytes = -64;
  bad_step.iter_decrement_bytes = 64;
  CHECK_FALSE(verify_vector_string_dtor_site(bad_step));

  // Near-miss 2: missing outer `free(begin)` call.
  vector_string_dtor_candidate_t bad_no_free = ok;
  bad_no_free.calls_free_on_begin = false;
  CHECK_FALSE(verify_vector_string_dtor_site(bad_no_free));

  // Near-miss 3: escaping loop temporary or external incoming goto label.
  vector_string_dtor_candidate_t bad_escape = ok;
  bad_escape.temps_do_not_escape = false;
  CHECK_FALSE(verify_vector_string_dtor_site(bad_escape));
  vector_string_dtor_candidate_t bad_label = ok;
  bad_label.has_external_incoming_label = true;
  CHECK_FALSE(verify_vector_string_dtor_site(bad_label));
}

TEST_CASE("M4-S4 C6.4: verify_vector_push_back_realloc_site positive and near-miss negative")
{
  using wasmhex::libcxx_detail::vector_push_back_candidate_t;
  using wasmhex::libcxx_detail::verify_vector_push_back_realloc_site;

  // Positive 1: 4-byte pointer elements in ExtractJoinRelations (&v770 and a4, max_size=0x40000000).
  vector_push_back_candidate_t ok4{};
  ok4.end_offset_from_begin = 4;
  ok4.cap_offset_from_begin = 8;
  ok4.elem_size = 4;
  ok4.max_size_constant = 0x40000000ULL;
  ok4.has_capacity_check = true;
  ok4.has_fast_path_store_and_advance = true;
  ok4.has_throw_length_error = true;
  ok4.has_bad_array_new_length = true;
  ok4.has_operator_new = true;
  ok4.has_new_elem_store = true;
  ok4.has_backward_copy_loop = true;
  ok4.has_vector_triple_update = true;
  ok4.has_old_buffer_free = true;
  ok4.has_final_end_store = true;
  ok4.has_external_incoming_label = false;
  ok4.temps_do_not_escape = true;
  CHECK(verify_vector_push_back_realloc_site(ok4));

  // Positive 2: 16-byte centroid elements in mergeProcessed (&v60, max_size=0x10000000).
  vector_push_back_candidate_t ok16 = ok4;
  ok16.elem_size = 16;
  ok16.max_size_constant = 0x10000000ULL;
  CHECK(verify_vector_push_back_realloc_site(ok16));

  // Near-miss 1: ExtractJoinRelations site 2 where shared LABEL_651 has external incoming gotos.
  vector_push_back_candidate_t bad_ext_lbl = ok4;
  bad_ext_lbl.has_external_incoming_label = true;
  CHECK_FALSE(verify_vector_push_back_realloc_site(bad_ext_lbl));

  // Near-miss 2: mismatched max_size_constant vs elem_size (e.g. 0x10000000 for 4-byte elem).
  vector_push_back_candidate_t bad_max = ok4;
  bad_max.max_size_constant = 0x10000000ULL;
  CHECK_FALSE(verify_vector_push_back_realloc_site(bad_max));

  // Near-miss 3: missing backward copy loop or missing old buffer free (reserve/resize, not push_back).
  vector_push_back_candidate_t bad_no_copy = ok4;
  bad_no_copy.has_backward_copy_loop = false;
  CHECK_FALSE(verify_vector_push_back_realloc_site(bad_no_copy));
}

TEST_CASE("M4-S3: is_invoke_helper_name and match_legalize_i64_wrapper_body")
{
  // The recognizer moved to wasmhex::loader (core/toolchains/go/eh).
  using wasmhex::loader::is_invoke_helper_name;
  using wasmhex::loader::match_legalize_i64_wrapper_body;

  // invoke_[vijfd]+ helper names.
  CHECK(is_invoke_helper_name("invoke_v"));
  CHECK(is_invoke_helper_name("invoke_vi"));
  CHECK(is_invoke_helper_name("invoke_vij"));
  CHECK(is_invoke_helper_name("invoke_ji"));
  CHECK(is_invoke_helper_name("invoke_dij"));
  CHECK(is_invoke_helper_name("invoke_fijjjjij"));
  CHECK(is_invoke_helper_name("invoke_jjjjjjd"));
  CHECK_FALSE(is_invoke_helper_name(nullptr));
  CHECK_FALSE(is_invoke_helper_name("invoke_"));
  CHECK_FALSE(is_invoke_helper_name("invoke_vix"));
  CHECK_FALSE(is_invoke_helper_name("invoke_vi_1"));
  CHECK_FALSE(is_invoke_helper_name("_embind_register_bigint"));

  // Positive shape (a): arg-passing wrapper (duckdb wasm_func_62934 -> call 202 invoke_vij):
  // local.get 0; local.get 1; local.get 2; i32.wrap_i64; local.get 2; i64.const 32;
  // i64.shr_u; i32.wrap_i64; call 202; end
  const std::vector<std::uint8_t> shape_a = {
    0x20, 0x00, 0x20, 0x01, 0x20, 0x02, 0xA7, 0x20, 0x02,
    0x42, 0x20, 0x88, 0xA7, 0x10, 0xCA, 0x01, 0x0B,
  };
  std::uint32_t callee_idx = 0;
  REQUIRE(match_legalize_i64_wrapper_body(shape_a.data(), shape_a.size(), &callee_idx));
  CHECK(callee_idx == 202u);

  // Positive shape (b): i64-returning wrapper (duckdb wasm_func_62930 -> call 198 invoke_ji):
  // local.get 0; local.get 1; call 198; i64.extend_i32_u; global.get 292;
  // i64.extend_i32_u; i64.const 32; i64.shl; i64.or; end
  const std::vector<std::uint8_t> shape_b = {
    0x20, 0x00, 0x20, 0x01, 0x10, 0xC6, 0x01, 0xAD, 0x23,
    0xA4, 0x02, 0xAD, 0x42, 0x20, 0x86, 0x84, 0x0B,
  };
  callee_idx = 0;
  REQUIRE(match_legalize_i64_wrapper_body(shape_b.data(), shape_b.size(), &callee_idx));
  CHECK(callee_idx == 198u);

  // Positive combined shape (a)+(b) (duckdb wasm_func_62951 -> call 220 invoke_jj):
  const std::vector<std::uint8_t> shape_ab = {
    0x20, 0x00, 0x20, 0x01, 0xA7, 0x20, 0x01, 0x42, 0x20, 0x88, 0xA7, 0x10,
    0xDC, 0x01, 0xAD, 0x23, 0xA4, 0x02, 0xAD, 0x42, 0x20, 0x86, 0x84, 0x0B,
  };
  callee_idx = 0;
  REQUIRE(match_legalize_i64_wrapper_body(shape_ab.data(), shape_ab.size(), &callee_idx));
  CHECK(callee_idx == 220u);

  // Negative near-miss 1: plain passthrough without i64 split or return combine.
  const std::vector<std::uint8_t> bad_plain = {
    0x20, 0x00, 0x20, 0x01, 0x10, 0xCA, 0x01, 0x0B,
  };
  CHECK_FALSE(match_legalize_i64_wrapper_body(bad_plain.data(), bad_plain.size(), &callee_idx));

  // Negative near-miss 2: extra constant argument (like duckdb wasm_func_62928 / 63018).
  const std::vector<std::uint8_t> bad_const_arg = {
    0x20, 0x00, 0x20, 0x01, 0x41, 0x08, 0x20, 0x02, 0xA7, 0x20,
    0x02, 0x42, 0x20, 0x88, 0xA7, 0x10, 0xC4, 0x01, 0x0B,
  };
  CHECK_FALSE(
    match_legalize_i64_wrapper_body(bad_const_arg.data(), bad_const_arg.size(), &callee_idx));

  // Negative near-miss 3: wrong shift amount (16 instead of 32).
  const std::vector<std::uint8_t> bad_shift = {
    0x20, 0x00, 0x20, 0x01, 0x20, 0x02, 0xA7, 0x20, 0x02,
    0x42, 0x10, 0x88, 0xA7, 0x10, 0xCA, 0x01, 0x0B,
  };
  CHECK_FALSE(match_legalize_i64_wrapper_body(bad_shift.data(), bad_shift.size(), &callee_idx));

  // Negative near-miss 4: mismatched local index in split high half.
  const std::vector<std::uint8_t> bad_local_order = {
    0x20, 0x00, 0x20, 0x01, 0x20, 0x02, 0xA7, 0x20, 0x01,
    0x42, 0x20, 0x88, 0xA7, 0x10, 0xCA, 0x01, 0x0B,
  };
  CHECK_FALSE(
    match_legalize_i64_wrapper_body(bad_local_order.data(), bad_local_order.size(), &callee_idx));

  // Negative near-miss 5: trailing drop after call (like duckdb wasm_func_62992).
  const std::vector<std::uint8_t> bad_trailing_drop = {
    0x20, 0x00, 0x20, 0x01, 0x20, 0x02, 0xA7, 0x20, 0x02,
    0x42, 0x20, 0x88, 0xA7, 0x10, 0xCA, 0x01, 0x1A, 0x0B,
  };
  CHECK_FALSE(match_legalize_i64_wrapper_body(bad_trailing_drop.data(), bad_trailing_drop.size(),
                                              &callee_idx));
}

TEST_CASE("M4-S2: split_demangled_callee_span and strip_collision_suffix_len")
{
  auto split = [](const std::string &demangled) -> std::string
  {
    size_t start = 0;
    size_t end = 0;
    if ( !wasmhex::idiom_detail::split_demangled_callee_span(demangled.c_str(), demangled.size(),
                                                             &start, &end) )
      return std::string();
    return demangled.substr(start, end - start);
  };

  // Leading return types (void, pointer, reference, template return type) are stripped.
  CHECK(split("duckdb::LogicalProjection & "
              "duckdb::LogicalOperator::Cast<duckdb::LogicalProjection>(duckdb::LogicalOperator &)")
        == "duckdb::LogicalOperator::Cast<duckdb::LogicalProjection>");
  CHECK(split("void std::__2::__hash_table<std::__2::__hash_value_type<unsigned long long,"
              "duckdb::RelationStats>,std::__2::allocator<duckdb::RelationStats>>"
              "::__assign_multi<std::__2::__hash_const_iterator<void *> *>(void *, void *)")
        == "std::__2::__hash_table<std::__2::__hash_value_type<unsigned long long,"
           "duckdb::RelationStats>,std::__2::allocator<duckdb::RelationStats>>"
           "::__assign_multi<std::__2::__hash_const_iterator<void *> *>");
  CHECK(split("duckdb::RelationStats* std::__2::vector<duckdb::RelationStats,"
              "std::__2::allocator<duckdb::RelationStats>>::__push_back_slow_path<"
              "duckdb::RelationStats const&>(duckdb::RelationStats const&)")
        == "std::__2::vector<duckdb::RelationStats,std::__2::allocator<duckdb::RelationStats>>"
           "::__push_back_slow_path<duckdb::RelationStats const&>");
  CHECK(
    split("void std::__2::vector<duckdb::RelationStats,std::__2::allocator<"
          "duckdb::RelationStats>>::__init_with_size[abi:ne180100]<duckdb::RelationStats*,"
          "duckdb::RelationStats*>(duckdb::RelationStats*, duckdb::RelationStats*, unsigned long)")
    == "std::__2::vector<duckdb::RelationStats,std::__2::allocator<duckdb::RelationStats>>"
       "::__init_with_size[abi:ne180100]<duckdb::RelationStats*,duckdb::RelationStats*>");

  // Constructors, destructors, and operators keep their qualified names intact.
  CHECK(split("duckdb::JoinOrderOptimizer::~JoinOrderOptimizer()")
        == "duckdb::JoinOrderOptimizer::~JoinOrderOptimizer");
  CHECK(
    split("duckdb::Value const & duckdb::Vector::operator[]<unsigned long>(unsigned long) const")
    == "duckdb::Vector::operator[]<unsigned long>");
  CHECK(split("bool duckdb::Value::operator()(int, int (*)(double)) const")
        == "duckdb::Value::operator()");
  CHECK(split("duckdb::Value::operator duckdb::uhugeint_t() const")
        == "duckdb::Value::operator duckdb::uhugeint_t");
  CHECK(split("operator new(unsigned long)").empty());

  // Collision suffix stripping preserves Itanium T0_ underscores.
  size_t len = 0;
  const char *s1 = "_ZNK6duckdb12optional_ptrINS_15JoinRelationSetELb1EE10CheckValidEv_427";
  REQUIRE(wasmhex::idiom_detail::strip_collision_suffix_len(s1, &len));
  CHECK(std::string(s1, len)
        == "_ZNK6duckdb12optional_ptrINS_15JoinRelationSetELb1EE10CheckValidEv");

  const char *s2 = "_ZNSt3__212__hash_tableIyE25__emplace_unique_key_argsERKT_DpOT0__2082";
  REQUIRE(wasmhex::idiom_detail::strip_collision_suffix_len(s2, &len));
  CHECK(std::string(s2, len) == "_ZNSt3__212__hash_tableIyE25__emplace_unique_key_argsERKT_DpOT0_");

  CHECK_FALSE(wasmhex::idiom_detail::strip_collision_suffix_len("_Znwm", &len));
}

TEST_CASE("C5: is_valid_eh_landing_pad and verify_eh_empty_pad_collapse")
{
  using wasmhex::eh_detail::classify_eh_pad_collapse;
  using wasmhex::eh_detail::eh_landing_pad_candidate_t;
  using wasmhex::eh_detail::eh_pad_skip_reason_name;
  using wasmhex::eh_detail::eh_pad_skip_reason_t;
  using wasmhex::eh_detail::eh_region_stats_t;
  using wasmhex::eh_detail::is_valid_eh_landing_pad;
  using wasmhex::eh_detail::verify_eh_empty_pad_collapse;

  // Positive 1 (c2 empty-cleanup collapse): duckdb __shared_ptr_emplace (0x15c372),
  // PendingStatementOrPreparedStatementInternal (0xc7c72a), ToDaysFun::GetFunction (0x1096a49),
  // Log10Fun::GetFunction (0x1160ae6), and __permissions (0x1845875):
  //   matching_catch_2 = __cxa_find_matching_catch_2();
  //   __resumeException(matching_catch_2);
  //   __builtin_trap();
  eh_landing_pad_candidate_t empty_ok{};
  empty_ok.starts_with_find_matching_catch = true;
  empty_ok.is_catch_all = true;
  empty_ok.ends_with_resume_and_trap = true;
  empty_ok.resume_uses_catch_var = true;
  empty_ok.cleanup_stmt_count = 0;
  empty_ok.all_cleanup_stmts_calls_or_assigns = true;
  empty_ok.guard_predecessor_count = 2; // e.g. __permissions has 2 guards (inline + goto LABEL_17)
  empty_ok.non_guard_predecessor_count = 0;
  empty_ok.all_guards_adjacent_to_invoke = true;
  empty_ok.catch_temp_does_not_escape = true;
  CHECK(is_valid_eh_landing_pad(empty_ok));
  CHECK(verify_eh_empty_pad_collapse(empty_ok));
  CHECK(classify_eh_pad_collapse(empty_ok) == eh_pad_skip_reason_t::none);
  CHECK(std::strcmp(eh_pad_skip_reason_name(eh_pad_skip_reason_t::none), "none") == 0);

  // Positive 2 (c1 non-empty cleanup pad): AddRelationTdom / ExecuteTask / mergeProcessed:
  // valid EH landing pad (annotated by c1), skipped for c2 collapse with non_empty_cleanup.
  eh_landing_pad_candidate_t cleanup_pad = empty_ok;
  cleanup_pad.cleanup_stmt_count = 2;
  CHECK(is_valid_eh_landing_pad(cleanup_pad));
  CHECK_FALSE(verify_eh_empty_pad_collapse(cleanup_pad));
  CHECK(classify_eh_pad_collapse(cleanup_pad) == eh_pad_skip_reason_t::non_empty_cleanup);

  // Near-miss 1: typed catch clause (__cxa_find_matching_catch_3(typeinfo) in PendingStatement).
  eh_landing_pad_candidate_t bad_typed_catch = empty_ok;
  bad_typed_catch.is_catch_all = false;
  CHECK(is_valid_eh_landing_pad(bad_typed_catch));
  CHECK_FALSE(verify_eh_empty_pad_collapse(bad_typed_catch));
  CHECK(classify_eh_pad_collapse(bad_typed_catch) == eh_pad_skip_reason_t::missing_catch_call);

  // Near-miss 2: missing __resumeException(v) or mismatched exception variable.
  eh_landing_pad_candidate_t bad_resume = empty_ok;
  bad_resume.resume_uses_catch_var = false;
  CHECK_FALSE(verify_eh_empty_pad_collapse(bad_resume));
  CHECK(classify_eh_pad_collapse(bad_resume) == eh_pad_skip_reason_t::missing_resume_or_trap);

  eh_landing_pad_candidate_t bad_no_trap = empty_ok;
  bad_no_trap.ends_with_resume_and_trap = false;
  CHECK_FALSE(is_valid_eh_landing_pad(bad_no_trap));
  CHECK_FALSE(verify_eh_empty_pad_collapse(bad_no_trap));
  CHECK(classify_eh_pad_collapse(bad_no_trap) == eh_pad_skip_reason_t::missing_resume_or_trap);

  // Near-miss 3: cleanup contains disallowed control flow / side effects.
  eh_landing_pad_candidate_t bad_cleanup_kind = empty_ok;
  bad_cleanup_kind.all_cleanup_stmts_calls_or_assigns = false;
  CHECK_FALSE(is_valid_eh_landing_pad(bad_cleanup_kind));
  CHECK_FALSE(verify_eh_empty_pad_collapse(bad_cleanup_kind));
  CHECK(classify_eh_pad_collapse(bad_cleanup_kind)
        == eh_pad_skip_reason_t::non_call_or_assign_cleanup);

  // Near-miss 4: non-guard incoming goto edge targeting the pad label.
  eh_landing_pad_candidate_t bad_pred = empty_ok;
  bad_pred.non_guard_predecessor_count = 1;
  CHECK_FALSE(verify_eh_empty_pad_collapse(bad_pred));
  CHECK(classify_eh_pad_collapse(bad_pred) == eh_pad_skip_reason_t::unproven_predecessor_edge);

  // Near-miss 5: guard is not immediately adjacent to the invoke call.
  eh_landing_pad_candidate_t bad_non_adj = empty_ok;
  bad_non_adj.all_guards_adjacent_to_invoke = false;
  CHECK_FALSE(verify_eh_empty_pad_collapse(bad_non_adj));
  CHECK(classify_eh_pad_collapse(bad_non_adj) == eh_pad_skip_reason_t::non_adjacent_invoke_guard);

  // Near-miss 6: catch variable v_exc escapes the landing pad.
  eh_landing_pad_candidate_t bad_escape = empty_ok;
  bad_escape.catch_temp_does_not_escape = false;
  CHECK_FALSE(verify_eh_empty_pad_collapse(bad_escape));
  CHECK(classify_eh_pad_collapse(bad_escape) == eh_pad_skip_reason_t::escaping_pad_temp);

  // Stats accumulator check.
  eh_region_stats_t stats{};
  stats.pads_skipped_by_reason[std::size_t(eh_pad_skip_reason_t::non_empty_cleanup)] = 41;
  stats.pads_skipped_by_reason[std::size_t(eh_pad_skip_reason_t::unproven_predecessor_edge)] = 2;
  CHECK(stats.total_pads_skipped() == 43u);
}

TEST_CASE("C5-full: classify_eh_pad_move non-empty pad structuring plans")
{
  using wasmhex::eh_detail::classify_eh_pad_move;
  using wasmhex::eh_detail::eh_landing_pad_candidate_t;
  using wasmhex::eh_detail::eh_pad_move_plan_name;
  using wasmhex::eh_detail::eh_pad_move_plan_t;
  using wasmhex::eh_detail::eh_region_stats_t;

  // Base: a valid non-empty pad with proven exact-guard predecessors
  // (guards >= 1, no other edges, all adjacent to their invoke calls).
  eh_landing_pad_candidate_t tail_pad{};
  tail_pad.starts_with_find_matching_catch = true;
  tail_pad.is_catch_all = true;
  tail_pad.ends_with_resume_and_trap = true;
  tail_pad.resume_uses_catch_var = true;
  tail_pad.cleanup_stmt_count = 2; // e.g. `free(v7)` then resume — ExecuteTask shape
  tail_pad.all_cleanup_stmts_calls_or_assigns = true;
  tail_pad.guard_predecessor_count = 3; // e.g. ExtractJoinRelations LABEL_651
  tail_pad.non_guard_predecessor_count = 0;
  tail_pad.all_guards_adjacent_to_invoke = true;
  tail_pad.catch_temp_does_not_escape = true;

  // Positive: label-tail pad, disjoint body, no exterior jumps — movable.
  CHECK(classify_eh_pad_move(tail_pad) == eh_pad_move_plan_t::movable);
  CHECK(std::strcmp(eh_pad_move_plan_name(eh_pad_move_plan_t::movable), "movable") == 0);

  // Positive 2: pad already inline at its owning exact guard (ExecuteTask /
  // AddRelationTdom pad1) — final shape even when its tail is shared.
  eh_landing_pad_candidate_t inline_pad = tail_pad;
  inline_pad.entry_owned_by_guard = true;
  inline_pad.shared_body_stmts = 2; // AddRelationTdom pad2/pad3 LABEL_31 ladder
  CHECK(classify_eh_pad_move(inline_pad) == eh_pad_move_plan_t::already_inline);
  CHECK(std::strcmp(eh_pad_move_plan_name(eh_pad_move_plan_t::already_inline), "already_inline")
        == 0);

  // Not-candidate 1: an empty pad is collapse territory, not a move.
  eh_landing_pad_candidate_t empty_pad = tail_pad;
  empty_pad.cleanup_stmt_count = 0;
  CHECK(classify_eh_pad_move(empty_pad) == eh_pad_move_plan_t::not_candidate);

  // Not-candidate 2: invalid pad (cleanup is not call/assign only).
  eh_landing_pad_candidate_t invalid_pad = tail_pad;
  invalid_pad.all_cleanup_stmts_calls_or_assigns = false;
  CHECK(classify_eh_pad_move(invalid_pad) == eh_pad_move_plan_t::not_candidate);
  CHECK(std::strcmp(eh_pad_move_plan_name(eh_pad_move_plan_t::not_candidate), "not_candidate")
        == 0);

  // Blocker 1: non-owned entry with an unproven edge (plain tail block where
  // fall-in from the preceding sibling is not disproven).
  eh_landing_pad_candidate_t fallin_pad = tail_pad;
  fallin_pad.non_guard_predecessor_count = 1;
  CHECK(classify_eh_pad_move(fallin_pad) == eh_pad_move_plan_t::unproven_pred_edge);

  // Blocker 1b: no proven guard predecessor at all.
  eh_landing_pad_candidate_t guardless_pad = tail_pad;
  guardless_pad.guard_predecessor_count = 0;
  CHECK(classify_eh_pad_move(guardless_pad) == eh_pad_move_plan_t::unproven_pred_edge);

  // Blocker 1c: guards not adjacent to their invoke calls.
  eh_landing_pad_candidate_t nonadj_pad = tail_pad;
  nonadj_pad.all_guards_adjacent_to_invoke = false;
  CHECK(classify_eh_pad_move(nonadj_pad) == eh_pad_move_plan_t::unproven_pred_edge);
  CHECK(
    std::strcmp(eh_pad_move_plan_name(eh_pad_move_plan_t::unproven_pred_edge), "unproven_pred_edge")
    == 0);

  // Blocker 2: body shared with another pad's traced body (nested-scope
  // destructor ladders — ExtractJoinRelations LABEL_113..115..650..652).
  eh_landing_pad_candidate_t shared_pad = tail_pad;
  shared_pad.shared_body_stmts = 3;
  CHECK(classify_eh_pad_move(shared_pad) == eh_pad_move_plan_t::shared_cleanup_tail);
  CHECK(std::strcmp(eh_pad_move_plan_name(eh_pad_move_plan_t::shared_cleanup_tail),
                    "shared_cleanup_tail")
        == 0);

  // Blocker 3: exterior goto into an interior body label (another pad jumps
  // into the middle — AddRelationTdom pad3 `goto LABEL_31` into pad2).
  eh_landing_pad_candidate_t exterior_pad = tail_pad;
  exterior_pad.exterior_label_jumps = 1;
  CHECK(classify_eh_pad_move(exterior_pad) == eh_pad_move_plan_t::exterior_jump_in);
  CHECK(std::strcmp(eh_pad_move_plan_name(eh_pad_move_plan_t::exterior_jump_in), "exterior_jump_in")
        == 0);

  // Plan ordering: unproven edges block before shared tails.
  eh_landing_pad_candidate_t both_blocked = tail_pad;
  both_blocked.non_guard_predecessor_count = 1;
  both_blocked.shared_body_stmts = 2;
  CHECK(classify_eh_pad_move(both_blocked) == eh_pad_move_plan_t::unproven_pred_edge);

  // Stats accumulator: per-plan counters and label-tail-entry counter.
  eh_region_stats_t mstats{};
  ++mstats.pads_by_move_plan[std::size_t(eh_pad_move_plan_t::movable)];
  mstats.pads_by_move_plan[std::size_t(eh_pad_move_plan_t::already_inline)] = 41;
  mstats.pads_label_tail_entry = 2;
  CHECK(mstats.pads_by_move_plan[std::size_t(eh_pad_move_plan_t::movable)] == 1u);
  CHECK(mstats.pads_by_move_plan[std::size_t(eh_pad_move_plan_t::already_inline)] == 41u);
  CHECK(mstats.pads_label_tail_entry == 2u);
}

TEST_CASE("M4/D4 GĐ3: verify_sso_string_dtor_site honors the IR mark")
{
  using wasmhex::libcxx_detail::sso_string_dtor_candidate_t;
  using wasmhex::libcxx_detail::verify_sso_string_dtor_site;

  // A mark-proven candidate: the tag/data offset constants (the libc++
  // loader, so the ctree re-derivation may disagree — structuring checks
  // (else branch, labels, arg shape, same-base) still gate the rewrite.
  sso_string_dtor_candidate_t marked{};
  marked.is_signed_lt_zero = true;
  marked.is_free_call = true;
  marked.free_arg_count = 1;
  marked.data_read_size = 4;
  marked.is_pointer_form = true;
  marked.same_base_expr = true;
  marked.tag_read_size = 1;
  marked.tag_byte_offset = 99;
  marked.mark_proven = true;
  CHECK(verify_sso_string_dtor_site(marked));
  marked.has_else = true;
  CHECK_FALSE(verify_sso_string_dtor_site(marked));
  marked.has_else = false;
  marked.same_base_expr = false;
  CHECK_FALSE(verify_sso_string_dtor_site(marked));

  // Unmarked sites keep the exact offset rule (11 for a 1-byte tag).
  sso_string_dtor_candidate_t unmarked = marked;
  unmarked.mark_proven = false;
  unmarked.same_base_expr = true;
  unmarked.tag_byte_offset = 11;
  unmarked.data_byte_offset = 0;
  CHECK(verify_sso_string_dtor_site(unmarked));
  unmarked.tag_byte_offset = 99;
  CHECK_FALSE(verify_sso_string_dtor_site(unmarked));
}
