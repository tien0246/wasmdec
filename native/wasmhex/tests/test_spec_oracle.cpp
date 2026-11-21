#include "doctest.h"
#include "core/validation/validate.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace wasmhex::loader;

namespace
{

struct oracle_entry_t
{
  std::string fixture;
  std::string verdict; // valid | invalid | unknown
};

std::vector<oracle_entry_t> read_oracle()
{
  std::vector<oracle_entry_t> entries;
  std::ifstream in(std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/spec_oracle.tsv");
  std::string line;
  while ( std::getline(in, line) )
  {
    if ( line.empty() || line[0] == '#' )
      continue;
    std::istringstream fields(line);
    oracle_entry_t entry;
    std::getline(fields, entry.fixture, '\t');
    std::getline(fields, entry.verdict, '\t');
    entries.push_back(entry);
  }
  return entries;
}

std::string read_fixture(const std::string &name)
{
  std::ifstream in(std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/" + name + ".wasm",
                   std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool validate_fixture(const std::string &name, validation_verdict_t *verdict)
{
  const std::string bytes = read_fixture(name);
  REQUIRE_MESSAGE(!bytes.empty(), "missing fixture " << name);
  module_t module;
  *verdict = validation_verdict_t{};
  return validate_module(module, reinterpret_cast<const uchar *>(bytes.data()), bytes.size(),
                         verdict);
}

// Contract 1: SpecValidity

// Engine-invalid modules the core still ACCEPTS. Each entry is a tracked
// spec-compliance bug in the validator (not a policy choice): the function
// validator does not model that instruction family yet, so a definitely
// invalid module slips through as accepted.
const std::set<std::string> spec_validity_known_violations = {
  // wasmhex_atomics_invalid_stack removed from known violations: the
  // validator now models the full 0xFE atomics family.
};

// Optional per-fixture rejection-class expectations for engine-invalid
// fixtures whose rejection family is part of the contract.
const std::map<std::string, rejection_class_t> spec_invalid_expected_class = {
  { "a6_local_unassigned_bad",
    rejection_class_t::spec_invalid },                          // Unassigned non-defaultable local
  { "a7_local_count_bomb", rejection_class_t::resource_limit }, // Engine local-count cap
  { "a7_local_count_cap", rejection_class_t::resource_limit },  // Engine local-count cap
};

// Contract 2: BackendSupport

// Spec-valid modules wasmhex does not support: rejection is REQUIRED to
// classify as policy_subset (the honest "valid WebAssembly outside the
// supported subset" answer), never as a silent malformed_encoding.
const std::set<std::string> backend_unsupported_policy = {
  // typed refs / GC type forms
  "build_wasmhex_typed_ref_unsupported_gc",
  "wasmhex_typed_ref_unsupported_gc",
  // GC shapes beyond the modeled subset
  "link_gc_array_fixed_integer_chain_count_unsupported",
  "link_gc_array_new_fixed_computed_count_unsupported",
  "link_gc_array_ref_field_unsupported",
  "link_gc_struct_ref_func_field_unsupported",
  // call_ref shapes without statically known targets
  "wasmhex_return_call_indirect_mixed_register_unsupported",
  // EH shapes outside the single direct-call unwind family
  "f5_call_unwind_unsupported",
  "f5_legacy_eh_unsupported",
  "f5_legacy_rethrow_unsupported",
  "f5_try_table_unsupported",
};

// Spec-valid modules rejected WITHOUT a reason, so they classify as
// malformed_encoding instead of policy_subset. Each is a tracked
// misclassification bug: it needs a real failure_reason at its site.
const std::set<std::string> backend_known_malformed_violations = {
  "feature_extended_const_initializer",
  "link_comdat_members",
  "link_comdat_passive_prefix",
  "link_comdat_passive_winner",
  "link_comdat_prefix",
  "link_globals_a.o",
  "link_globals_data.o",
  "link_globals_init_resolves.o",
  "wasmhex_memory64_init",
};

// Fixtures intentionally outside the spec tsv: the brtable sweep family
// (covered by the validator's own list) and the source-side repro fixtures
// whose verdicts are pinned by the tests that build them. Note the sweep
// covers tests/fixtures and tests/fixtures/brtable only — deeper subdirs
// (g6/) are consumed by their own unit tests. Wired by NAME, not by count —
// a new fixture fails with its name until it is added to the tsv (or here
// with a reason), and a stale entry fails when its fixture is wired or
// removed.
const std::set<std::string> spec_oracle_unlisted_allowed = {
  "a1_call_indirect_arg_clobber",
  "a2_mangled_this_param",
  "a3_go_morestack_else",
  "bt_all_same",
  "bt_boundary",
  "bt_default_dup",
  "bt_depth_per_case_256",
  "bt_depth_per_case_64",
  "bt_depths",
  "bt_large",
  "bt_nested",
  "bt_payload",
  "bt_same_target",
  "bt_small",
  "bt_unreachable",
  "component_multi_core",
  "cpp4_memory_base_string",
  "g3_go_call_protocol",
  "g6_pclntab_names",
  "r2_arith_helpers",
  "s1_import_table_arg_clobber",
  "s10_cfg_incomplete_repro",
  "s11_go_zero_arg",
  "s2_go_sp_stable",
  "s5_typed_nullable_ref",
  "s7_go_f32_arg",
  "s7_go_provenance_bad_globals",
  "s7_go_provenance_bad_prologue",
  "s7_go_provenance_good",
  "s9_go_morestack_callee",
  "wasmhex_c_string_args",
};

} // namespace

TEST_CASE("SpecValidity: engine-invalid fixtures fail wasmhex validation")
{
  const std::vector<oracle_entry_t> oracle = read_oracle();
  REQUIRE(oracle.size() >= 300);
  size_t checked = 0;
  for ( const oracle_entry_t &entry : oracle )
  {
    if ( entry.verdict != "invalid" )
      continue;
    validation_verdict_t verdict;
    const bool accepted = validate_fixture(entry.fixture, &verdict);
    CAPTURE(entry.fixture);
    CAPTURE(verdict.reason ? verdict.reason : "(no reason)");
    ++checked;
    const bool tracked = spec_validity_known_violations.count(entry.fixture) != 0;
    if ( tracked )
    {
      // Tracked spec-compliance bug: the validator accepts an engine-invalid
      // module. Still asserted (never silent) so the entry cannot rot: fix
      // the modeling and remove it from the list.
      CHECK_MESSAGE(accepted,
                    "spec-violation fixed: remove " << entry.fixture
                                                    << " from spec_validity_known_violations");
      continue;
    }
    CHECK_MESSAGE(!accepted, "spec-invalid module accepted");
    if ( !accepted )
    {
      const auto expected = spec_invalid_expected_class.find(entry.fixture);
      if ( expected != spec_invalid_expected_class.end() )
        CHECK(verdict.rejection == expected->second);
      else
        CHECK(verdict.rejection != rejection_class_t::accepted);
    }
  }
  CHECK(checked == 14); // 10 original + a6/a7×2 wired in + simd lane-bounds
                        // grows only with the tsv
}

TEST_CASE("BackendSupport: engine-valid fixtures are accepted or rejected as policy")
{
  const std::vector<oracle_entry_t> oracle = read_oracle();
  size_t checked = 0;
  for ( const oracle_entry_t &entry : oracle )
  {
    if ( entry.verdict != "valid" )
      continue;
    validation_verdict_t verdict;
    const bool accepted = validate_fixture(entry.fixture, &verdict);
    CAPTURE(entry.fixture);
    CAPTURE(verdict.reason ? verdict.reason : "(no reason)");
    ++checked;
    if ( accepted )
    {
      // Default contract: spec-valid + not declared unsupported => accept.
      CHECK_MESSAGE(backend_unsupported_policy.count(entry.fixture) == 0,
                    "backend gap fixed: remove " << entry.fixture
                                                 << " from backend_unsupported_policy");
      CHECK_MESSAGE(backend_known_malformed_violations.count(entry.fixture) == 0,
                    "misclassification fixed: remove "
                      << entry.fixture << " from backend_known_malformed_violations");
      continue;
    }
    // Fixture wiring: the spec-valid fixtures of later rounds must land
    // in the right contract, not float between buckets.
    if ( entry.fixture == "a6_local_assigned_ok" )
      CHECK_MESSAGE(false, "a6_local_assigned_ok is fully supported: it must be accepted");
    if ( entry.fixture == "a5_unsupported_opcode" )
      CHECK_MESSAGE(false,
                    "a5_unsupported_opcode: A5 says an unmodeled opcode family "
                    "is a per-function 'unsupported' state, not a module rejection");
    if ( entry.fixture == "a8_table64_const_index" )
      CHECK_MESSAGE(false,
                    "a8_table64_const_index: table64 index forms must parse; "
                    "reject only as declared policy");
    if ( backend_unsupported_policy.count(entry.fixture) != 0 )
    {
      // Declared unsupported shape: the rejection MUST be an honest
      // policy_subset verdict with a capability reason (the policy table
      // in core/validate.hpp uses two reason families: "Unsupported ..."
      // and "Native wasmhex ..."). Anything else hides a bug.
      CHECK_MESSAGE(verdict.rejection == rejection_class_t::policy_subset,
                    "declared-unsupported fixture rejected as " << int(verdict.rejection)
                                                                << " (must be policy_subset)");
      REQUIRE(verdict.reason != nullptr);
      const std::string reason(verdict.reason);
      const bool capability_reason
        = reason.find("Unsupported") == 0 || reason.find("Native wasmhex") == 0;
      CHECK_MESSAGE(capability_reason, "reason is not a capability family: " << reason);
      continue;
    }
    if ( backend_known_malformed_violations.count(entry.fixture) != 0 )
    {
      // Tracked bug: valid module rejected as malformed. Still asserted so
      // the entry cannot rot; give the site a real reason and move the
      // fixture to backend_unsupported_policy (or accept it).
      CHECK_MESSAGE(verdict.rejection == rejection_class_t::malformed_encoding,
                    "misclassification changed: update the list for " << entry.fixture);
      continue;
    }
    // Undeclared rejection of a valid module: policy_subset/spec_invalid
    // are honest classifications
    // malformed_encoding is not.
    const bool ok_class = verdict.rejection == rejection_class_t::policy_subset
      || verdict.rejection == rejection_class_t::spec_invalid
      || verdict.rejection == rejection_class_t::resource_limit;
    CHECK_MESSAGE(ok_class, "valid module rejected as malformed (should be policy/spec/resource)");
  }
  CHECK(checked == 418); // 384 original + wired rounds + simd lane-bounds
}

TEST_CASE("release metric: every SpecValidity-invalid fixture is rejected")
{
  // Release gate: "a wrong module accepted" must be zero (or exactly the
  // tracked, named violations). This sweep validates EVERY .wasm under
  // tests/fixtures (not just tsv-listed names) so a new fixture cannot ship
  // unclassified, and cross-checks the real corpus modules still accept.
  const std::vector<oracle_entry_t> oracle = read_oracle();
  std::map<std::string, std::string> v8;
  for ( const oracle_entry_t &entry : oracle )
    v8[entry.fixture] = entry.verdict;

  size_t scanned = 0, invalid_rejected = 0, invalid_accepted = 0;
  size_t valid_accepted = 0, valid_rejected = 0, unknown = 0;
  std::vector<std::string> unlisted_names;
  std::vector<std::string> accepted_invalid, malformed_valid;
  for ( const char *root : { "/tests/fixtures", "/tests/fixtures/brtable" } )
  {
    const std::string dir = std::string(WASMHEX_REPO_DIR) + root;
    for ( const auto &entry : std::filesystem::directory_iterator(dir) )
    {
      const std::string path = entry.path().string();
      if ( path.size() < 5 || path.compare(path.size() - 5, 5, ".wasm") != 0 )
        continue;
      const std::string name = entry.path().stem().string();
      ++scanned;
      module_t module;
      validation_verdict_t verdict{};
      std::ifstream in(path, std::ios::binary);
      const std::string bytes((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
      REQUIRE_MESSAGE(!bytes.empty(), "unreadable fixture " << path);
      const bool accepted = validate_module(module, reinterpret_cast<const uchar *>(bytes.data()),
                                            bytes.size(), &verdict);
      const std::string verdict_v8 = v8.count(name) != 0 ? v8[name] : std::string();
      if ( verdict_v8 == "invalid" )
      {
        if ( accepted )
        {
          ++invalid_accepted;
          accepted_invalid.push_back(name);
        }
        else
          ++invalid_rejected;
      }
      else if ( verdict_v8 == "valid" )
      {
        if ( accepted )
          ++valid_accepted;
        else
        {
          ++valid_rejected;
          if ( verdict.rejection == rejection_class_t::malformed_encoding
               && backend_known_malformed_violations.count(name) == 0 )
            malformed_valid.push_back(name);
        }
      }
      else if ( verdict_v8 == "unknown" )
        ++unknown;
      else
        unlisted_names.push_back(name); // fixture without a tsv row: wire it in when adding
    }
  }

  // Real corpus modules must still accept (no false-positive rejections).
  for ( const char *module_name : { "duckdb", "main" } )
  {
    const std::string path
      = std::string(WASMHEX_REPO_DIR) + "/wasm-test/wasm-test/" + module_name + ".wasm";
    std::ifstream in(path, std::ios::binary);
    if ( !in.good() )
    {
      std::printf("RELEASE corpus module missing: %s\n", path.c_str());
      continue;
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    module_t module;
    validation_verdict_t verdict{};
    CHECK_MESSAGE(validate_module(module, reinterpret_cast<const uchar *>(bytes.data()),
                                  bytes.size(), &verdict),
                  module_name << ".wasm corpus module was rejected: "
                              << (verdict.reason ? verdict.reason : "(malformed)"));
  }

  std::printf("WASMHEX_RELEASE scanned=%zu invalid_rejected=%zu invalid_accepted=%zu "
              "valid_accepted=%zu valid_rejected=%zu unknown=%zu unlisted=%zu\n",
              scanned, invalid_rejected, invalid_accepted, valid_accepted, valid_rejected, unknown,
              unlisted_names.size());
  std::printf("WASMHEX_RELEASE_INVALID_ACCEPTED=%zu\n", invalid_accepted);
  for ( const std::string &name : accepted_invalid )
    std::printf("WASMHEX_RELEASE_INVALID_ACCEPTED_NAME %s%s\n", name.c_str(),
                spec_validity_known_violations.count(name) != 0 ? " (tracked)" : " (NEW!)");

  // Release condition: every accepted SpecValidity-invalid module must be a
  // named, tracked violation (the list shrinks to zero for release 0).
  CHECK(invalid_accepted == spec_validity_known_violations.size());
  for ( const std::string &name : accepted_invalid )
    CHECK_MESSAGE(spec_validity_known_violations.count(name) != 0,
                  "UNTRACKED invalid module accepted: " << name);
  // Undeclared malformed rejections of valid modules are regressions.
  CHECK(malformed_valid.empty());
  // Unlisted fixtures must be wired into the tsv when introduced — checked
  // by name against the allowlist above (two-sided: new fixtures fail until
  // wired, stale allowlist entries fail once wired/removed).
  const std::set<std::string> unlisted_set(unlisted_names.begin(), unlisted_names.end());
  CHECK(unlisted_set.size() == unlisted_names.size()); // directory names are unique
  for ( const std::string &name : unlisted_set )
    CHECK_MESSAGE(spec_oracle_unlisted_allowed.count(name) != 0,
                  "new fixture not wired into spec_oracle.tsv: " << name);
  for ( const std::string &name : spec_oracle_unlisted_allowed )
    CHECK_MESSAGE(unlisted_set.count(name) != 0,
                  "allowlist entry stale (fixture wired or removed): " << name);
}
