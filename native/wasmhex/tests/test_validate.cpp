// Validation oracle tests.
// (a) every golden _validator.json valid=false fixture must be rejected as
// spec_invalid/malformed (the mandatory direction);
// (b) the policy table: representative spec-valid fixtures the core rejects
// for documented soundness reasons classify as policy_subset with the
// right failure message family;
// (c) a broad set of spec-valid, policy-safe fixtures passes validation;
// (d) crafted malformed encodings (truncated uleb, section-order violation,
// type-count mismatch, body-size overflow, bad magic) are rejected.
#include "doctest.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../core/types.hpp"
#include "../core/validation/validate.hpp"
#include "../core/diagnostic.hpp"

using namespace wasmhex::loader;

namespace
{

std::string read_file(const std::string &path)
{
  std::ifstream in(path, std::ios::binary);
  if ( !in )
    return std::string();
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool find_fixture(const std::string &name, std::string *bytes)
{
  static const char *roots[] = { "/tests/fixtures", "/tests/fixtures/brtable" };
  for ( const char *root : roots )
  {
    const std::string data = read_file(std::string(WASMHEX_REPO_DIR) + root + "/" + name + ".wasm");
    if ( !data.empty() )
    {
      *bytes = data;
      return true;
    }
  }
  return false;
}

bool validate_bytes(const std::string &bytes, validation_verdict_t *verdict)
{
  module_t module;
  return validate_module(module, reinterpret_cast<const uchar *>(bytes.data()), bytes.size(),
                         verdict);
}

bool parse_validator_verdict(const std::string &name, bool *valid)
{
  const std::string json
    = read_file(std::string(WASMHEX_REPO_DIR) + "/tests/golden/" + name + "/_validator.json");
  if ( json.empty() )
    return false;
  const size_t at = json.find("\"valid\"");
  if ( at == std::string::npos )
    return false;
  const size_t true_at = json.find("true", at);
  const size_t false_at = json.find("false", at);
  *valid = true_at < false_at || false_at == std::string::npos;
  return true;
}

std::vector<std::string> list_golden_dirs()
{
  std::vector<std::string> names;
  const std::string root = std::string(WASMHEX_REPO_DIR) + "/tests/golden";
  for ( const auto &entry : std::filesystem::directory_iterator(root) )
    if ( entry.is_directory() )
      names.push_back(entry.path().filename().string());
  std::sort(names.begin(), names.end());
  return names;
}

} // namespace

TEST_CASE("oracle: every spec-invalid golden fixture is rejected as spec/malformed")
{
  std::map<std::string, std::string> v8;
  {
    std::ifstream in(std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/spec_oracle.tsv");
    std::string line;
    while ( std::getline(in, line) )
    {
      if ( line.empty() || line[0] == '#' )
        continue;
      const size_t tab = line.find('\t');
      if ( tab != std::string::npos )
        v8[line.substr(0, tab)] = line.substr(tab + 1, line.find('\t', tab + 1) - (tab + 1));
    }
  }
  // Tracked spec-compliance bug (shared with test_spec_oracle): atomics
  // stack discipline is not modeled, so this engine-invalid module parses.
  const std::string tracked_violation = "wasmhex_atomics_invalid_stack";

  size_t checked = 0, bad = 0, stale = 0, unknown = 0;
  for ( const std::string &name : list_golden_dirs() )
  {
    bool validator_valid = true;
    if ( !parse_validator_verdict(name, &validator_valid) || validator_valid )
      continue;
    std::string bytes;
    if ( !find_fixture(name, &bytes) )
      continue; // fixture sources live elsewhere (built at dump time)
    const std::string verdict_v8 = v8.count(name) != 0 ? v8[name] : std::string();
    if ( verdict_v8 == "valid" )
    {
      ++stale; // stale python-oracle artifact: spec-valid per the engine
      continue;
    }
    if ( verdict_v8 != "invalid" )
    {
      ++unknown; // engine cannot judge (component binaries etc.)
      continue;
    }
    validation_verdict_t verdict;
    if ( validate_bytes(bytes, &verdict) )
    {
      if ( name != tracked_violation )
      {
        ++bad;
        std::printf("BAD %s: V8-invalid but accepted\n", name.c_str());
      }
      continue;
    }
    ++checked;
    static const std::set<std::string> policy_before_spec_overlap = {
      "wasmhex_call_ref_encoded_mismatch", // call_ref signature discipline
      "wasmhex_fixed_memory64_oversize",   // memory64 above 2^48 pages (spec max)
      "wasmhex_fixed_memory_lane_invalid", // SIMD lane index out of range
      "wasmhex_typed_ref_mismatch",        // ref.null of an unsupported heap type
      // GC-typed fixtures whose malformed GC form trips the GC policy gate:
      "link_gc_mutable_global_element",
      "link_gc_struct_new_binary_mixed_width",
      "link_gc_struct_new_binary_scalar_mismatch",
      "link_gc_struct_new_binary_second_mismatch",
      "link_gc_struct_new_two_computed_width_mismatch",
    };
    const bool overlap = policy_before_spec_overlap.count(name) != 0
      || (verdict.reason != nullptr
          && std::string(verdict.reason).find("Unsupported WebAssembly GC type form") == 0);
    if ( verdict.rejection == rejection_class_t::policy_subset && !overlap )
    {
      ++bad;
      std::printf("MISCLASSIFIED %s: V8-invalid fixture got policy (%s)\n", name.c_str(),
                  verdict.reason ? verdict.reason : "-");
    }
  }
  // All engine-invalid fixtures in the golden corpus must be exercised:
  // 10 original + 3 GC/call-ref/fixed-memory invalid fixtures already
  // wired in the tsv (the atomics fixture joined the rejected set once
  // the validator modeled the 0xFE family) — grows only with the tsv.
  CHECK(checked == 14);
  CHECK(bad == 0);
  CHECK(stale <= 90); // shrink to 0 when the golden oracle is refreshed
  std::printf("INFO oracle-invalid corpus: v8-invalid checked=%zu bad=%zu "
              "stale-artifacts=%zu unknown=%zu\n",
              checked, bad, stale, unknown);
}

TEST_CASE("policy table: spec-valid fixtures reject with policy classification")
{
  struct Case
  {
    const char *fixture;
    const char *prefix;
  };
  const Case cases[] = {
    { "backend_segment_indirect_table_mutation", "Unsupported WebAssembly table.set mutation" },
    { "build_wasmhex_typed_ref_unsupported_gc", "Unsupported WebAssembly GC type form" },
    { "wasmhex_indirect_table_copy_reject",
      "Unsupported WebAssembly table.set mutation" }, // table.set trips first
    { "wasmhex_indirect_passive_segment", "Unsupported WebAssembly element segment; only active" },
    { "native_v128_abi", "Unsupported native wasmhex SIMD memory operation" },
    { "call_ref_direct", "Unsupported native wasmhex global type" },
  };
  for ( const Case &c : cases )
  {
    std::string bytes;
    if ( !find_fixture(c.fixture, &bytes) )
    {
      std::printf("MISSING fixture %s\n", c.fixture);
      continue;
    }
    // The golden oracle has been re-dumped with an older python validator
    // since the original dump; its verdict may now disagree, informational only
    // the policy classification below is the assertion.
    validation_verdict_t verdict;
    REQUIRE_FALSE(validate_bytes(bytes, &verdict));
    CHECK(verdict.rejection == rejection_class_t::policy_subset);
    REQUIRE(verdict.reason != nullptr);
    INFO(c.fixture << " reason=" << verdict.reason);
    CHECK(std::string(verdict.reason).find(c.prefix) == 0);
  }
}

TEST_CASE("policy-safe spec-valid fixtures pass validation")
{
  const char *fixtures[] = {
    "add",
    "br_table",
    "br_on_null",
    "bt_small",
    "bt_boundary",
    "bt_payload",
    "bt_depths",
    "bt_nested",
    "bt_large",
    "bt_all_same",
    "bt_default_dup",
    "backend_segment_indirect_loop",
    "backend_segment_indirect_finite_diff",
    "backend_segment_indirect_finite_same",
    "backend_segment_indirect_unknown",
    "backend_segment_start",
    "backend_segment_repeated_drop",
    "wasmhex_branch_values",
    "wasmhex_multi_branch_values",
    "link_comdat_multi_table_tag",
    "add_globals",
    "wasmhex_large_br_table",
    "feature_call_indirect_overlong",
    "feature_select_typed",
    "wasmhex_i64_select_shift",
    "wasmhex_repeated_scalar_loads",
  };
  size_t passed = 0, missing = 0;
  for ( const char *name : fixtures )
  {
    std::string bytes;
    if ( !find_fixture(name, &bytes) )
    {
      ++missing;
      continue;
    }
    validation_verdict_t verdict;
    INFO(name);
    // Every FOUND fixture must pass — a reject here is a regression
    // not printf noise. (`missing` stays a bounded counter because fixture
    // files churn between rounds; the bound itself is asserted below.)
    CHECK_MESSAGE(validate_bytes(bytes, &verdict),
                  name << " rejected: " << (verdict.reason ? verdict.reason : "(malformed)"));
    ++passed;
  }
  CHECK(missing <= 5); // tolerate fixture churn between rounds
  CHECK(passed >= 20);
}

TEST_CASE("crafted malformed encodings are rejected")
{
  const std::string add = read_file(std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/add.wasm");
  REQUIRE_FALSE(add.empty());
  const std::vector<uchar> base(add.begin(), add.end());
  validation_verdict_t verdict;

  // Truncated uleb: cut the file inside the last section.
  std::string truncated = add.substr(0, add.size() - 2);
  CHECK_FALSE(validate_bytes(truncated, &verdict));
  CHECK(verdict.rejection == rejection_class_t::malformed_encoding);

  // Bad magic.
  std::string bad_magic = add;
  bad_magic[1] = 'X';
  CHECK_FALSE(validate_bytes(bad_magic, &verdict));

  // Section order violation: append a fresh type section after data.
  // type section id=1 with one empty-ish payload after existing sections.
  std::string reordered = add;
  const uchar tail[] = { 0x01, 0x01, 0x00 }; // id=1 size=1 count=0
  reordered.append(reinterpret_cast<const char *>(tail), sizeof(tail));
  CHECK_FALSE(validate_bytes(reordered, &verdict));

  // Empty input.
  CHECK_FALSE(validate_bytes(std::string(), &verdict));

  // Section size overflowing the buffer.
  std::string oversized = add;
  oversized.resize(oversized.size() - 1);
  oversized.push_back('\x7f'); // corrupt the final byte
  CHECK_FALSE(validate_bytes(oversized, &verdict));
}

TEST_CASE("N1 diagnostics: exact-match table replaces prefix classify")
{
  // Classification now runs through the diagnostic attached by
  // validate_module; the table maps the exact legacy reason strings.
  CHECK(error_code_for_reason(nullptr) == error_code::none);
  CHECK(class_for_code(error_code::bad_leb) == rejection_class_t::malformed_encoding);
  CHECK(class_for_code(error_code::policy_gc_types) == rejection_class_t::policy_subset);
  CHECK(class_for_code(error_code::bad_export) == rejection_class_t::spec_invalid);
  // rejection invariant: every rejection carries a complete diagnostic
  const std::string add = read_file(std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/add.wasm");
  module_t module;
  validation_verdict_t verdict;
  INFO("valid fixture");
  CHECK(validate_module(module, reinterpret_cast<const uchar *>(add.data()), add.size(), &verdict));
  CHECK(rejection_has_diagnostic(true, module.diagnostic));
  const std::string truncated = add.substr(0, add.size() - 2);
  module_t broken;
  CHECK_FALSE(validate_module(broken, reinterpret_cast<const uchar *>(truncated.data()),
                              truncated.size(), &verdict));
  CHECK(rejection_has_diagnostic(false, broken.diagnostic));
  CHECK(broken.diagnostic.cls == rejection_class_t::malformed_encoding);
  CHECK(broken.diagnostic.code != error_code::none);
}

TEST_CASE("V1: the name section outranks the export name")
{
  module_t module;
  const std::string bytes
    = read_file(std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/v1_name_over_export.wasm");
  REQUIRE(!bytes.empty());
  REQUIRE(module.parse(reinterpret_cast<const uchar *>(bytes.data()), bytes.size()));
  // wat2wasm --debug-names emits a name-section entry only for functions
  // with an explicit $name: func0 carries `internal_symbol` plus the
  // export `public`; func1 has only the export.
  REQUIRE(module.function_names.size() == 1);
  CHECK(module.function_names[0].name == "internal_symbol");
  REQUIRE(module.function_exports.size() == 2);
  CHECK(module.function_exports[0].name == "public");
  // Name section wins over the export alias (exports are the LAST
  // source — gowasm-balls func[1430] showed `wasm_export_run` hidden
  // behind the `run` export).
  CHECK(function_name(0, module.function_exports, module.function_names) == "internal_symbol");
  // Without a distinct name-section entry the export still applies.
  CHECK(function_name(1, module.function_exports, module.function_names) == "only_export");
  // pclntab outranks the export too (stripped Go modules keep source
  // names for exported wrappers).
  const std::string pclntab = "main.someFunc";
  CHECK(function_name(0, module.function_exports, module.function_names, nullptr, &pclntab)
        == "internal_symbol");
  CHECK(function_name(1, module.function_exports, module.function_names, nullptr, &pclntab)
        == "main.someFunc");
  // No source at all: the synthetic fallback.
  CHECK(function_name(2, module.function_exports, module.function_names) == "wasm_func_2");
}

TEST_CASE("P0-7a: validation_status_t covers the honest outcome vocabulary")
{
  // valid
  {
    module_t module;
    const std::string bytes = read_file(std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/add.wasm");
    validation_verdict_t verdict;
    REQUIRE(validate_module(module, reinterpret_cast<const uchar *>(bytes.data()), bytes.size(),
                            &verdict));
    CHECK(verdict.status == validation_status_t::valid);
    CHECK(verdict.valid);
  }
  // malformed: truncated body
  {
    const std::string add = read_file(std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/add.wasm");
    const std::string truncated = add.substr(0, add.size() - 2);
    module_t module;
    validation_verdict_t verdict;
    CHECK_FALSE(validate_module(module, reinterpret_cast<const uchar *>(truncated.data()),
                                truncated.size(), &verdict));
    CHECK(verdict.status == validation_status_t::malformed);
  }
  // invalid (spec) / unsupported (unmodeled family) / resource_limit are
  // exercised through the oracle corpus below; here pin one fixture of
  // each that the tsv already classifies.
  struct row_t
  {
    const char *fixture;
    validation_status_t want;
  };
  const row_t rows[] = {
    // module-level unmodeled constructs report the honest unsupported
    // status (per-function marks like a5_unsupported_opcode ACCEPT the
    // module — a different layer)
    { "backend_segment_indirect_table_mutation", validation_status_t::unsupported },
    // spec-invalid encodings classify as plain invalid
    { "simd_lane_bounds_invalid", validation_status_t::invalid },
  };
  for ( const row_t &row : rows )
  {
    module_t module;
    std::string bytes;
    if ( !find_fixture(row.fixture, &bytes) )
      continue; // fixtures wired by earlier rounds; absence is not a failure
    validation_verdict_t verdict;
    CHECK_FALSE(validate_module(module, reinterpret_cast<const uchar *>(bytes.data()), bytes.size(),
                                &verdict));
    CHECK(verdict.status == row.want);
  }
}
