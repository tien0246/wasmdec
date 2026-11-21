// Go toolchain profile — version parsing, family mapping, the
// three-layer resolver, and the corpus ground truth.
#include "../core/toolchains/go/profile.hpp"
#include "../core/binary/instruction_stream.hpp"

#define WASMHEX_PURE_STRING_TEST
#include "../backend/hexrays/ctree_presentation/rewriter_go.cpp"
#undef WASMHEX_PURE_STRING_TEST

#include "doctest.h"

#include "corpus_guard.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#include "../core/types.hpp"

using namespace wasmhex::loader;

namespace
{
std::string read_file(const std::string &path)
{
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool parse_module_file(const std::string &path, module_t *module)
{
  const std::string bytes = read_file(path);
  if ( bytes.empty() )
    return false;
  return module->parse(reinterpret_cast<const uchar *>(bytes.data()), bytes.size());
}
const module_t *cached_main126()
{
  static module_t mod;
  static bool loaded = false;
  static bool ok = false;
  if ( !loaded )
  {
    loaded = true;
    ok = parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/wasm-test/main.wasm", &mod);
  }
  return ok ? &mod : nullptr;
}

// Cache the remaining Go corpora/modules by repo-relative path so repeated
// census sweeps parse each module once per core_tests run.
const module_t *cached_go_module(const char *rel_path)
{
  static std::map<std::string, std::unique_ptr<module_t>> cache;
  auto it = cache.find(rel_path);
  if ( it != cache.end() )
    return it->second.get();
  auto mod = std::make_unique<module_t>();
  const bool ok = parse_module_file(std::string(WASMHEX_REPO_DIR) + rel_path, mod.get());
  // Never cache a failed parse: a later call with the same path must keep
  // seeing nullptr, not a pointer to a default-constructed (empty) module —
  // on a corpus-less public clone that empty module slipped past the
  // `if (cached_go_module(...))` guards and failed the census floors.
  if ( !ok )
    return nullptr;
  const module_t *result = mod.get();
  cache.emplace(rel_path, std::move(mod));
  return result;
}

// Loader-order display-name lookup (export names first, then pclntab) —
// the same priority go_body_display_name applies. Corpus ground truths are
// anchored by stable symbol names instead of body indices or magic numbers.
std::size_t body_index_for_name(const module_t &mod, const char *want)
{
  for ( std::size_t i = 0; i < mod.bodies.size(); ++i )
  {
    const std::size_t fn_index = mod.imported_func_types.size() + i;
    for ( const export_t &entry : mod.function_names )
      if ( entry.index == fn_index && entry.name == want )
        return i;
    if ( i < mod.pclntab_function_names.size() && mod.pclntab_function_names[i] == want )
      return i;
  }
  return std::size_t(SIZE_MAX);
}
} // namespace

TEST_CASE("G0: version strings map to profile families")
{
  int major = -1, minor = -1;
  CHECK(parse_go_version_string("go1.26.5", &major, &minor));
  CHECK(major == 1);
  CHECK(minor == 26);
  CHECK(go_profile_for_version(1, 26) == go_profile_t::go1_26_series);
  CHECK(go_profile_for_version(1, 27) == go_profile_t::go1_27_series);
  CHECK(go_profile_for_version(1, 25) == go_profile_t::go1_23_series);
  CHECK(go_profile_for_version(1, 21) == go_profile_t::go1_23_series);
  CHECK(go_profile_for_version(1, 20) == go_profile_t::go1_19_legacy);
  CHECK(go_profile_for_version(1, 19) == go_profile_t::go1_19_legacy);
  CHECK(go_profile_for_version(1, 28) == go_profile_t::unknown); // no corpus, no guess
  CHECK_FALSE(parse_go_version_string("1.26", &major, &minor));
  CHECK_FALSE(parse_go_version_string("golang1.26", &major, &minor));
  CHECK_FALSE(parse_go_version_string("", &major, &minor));
}

TEST_CASE("G0: expectations table differentiates the families")
{
  go_profile_info_t info;
  info.profile = go_profile_t::go1_27_series;
  info.apply_expectations();
  CHECK(info.v_registers);
  CHECK(info.wasmexport_wrapper);
  CHECK(info.pc_f_shift == 16);
  CHECK(info.wasm_ptr_is_load);
  CHECK(info.max_blocks_per_function == 65536);

  info.profile = go_profile_t::go1_26_series;
  info.apply_expectations();
  CHECK(info.v_registers);
  CHECK_FALSE(info.wasmexport_wrapper);
  CHECK(info.pc_f_shift == 16);
  CHECK_FALSE(info.wasm_ptr_is_load);

  info.profile = go_profile_t::go1_23_series;
  info.apply_expectations();
  CHECK_FALSE(info.v_registers);
  CHECK(info.pc_f_shift == 0);

  info.profile = go_profile_t::go1_19_legacy;
  info.apply_expectations();
  CHECK_FALSE(info.v_registers);
  CHECK(info.max_blocks_per_function == 0);
}

TEST_CASE("G0: layer contradiction lands on unknown with a diagnostic")
{
  module_t module;
  module.is_go = true;
  module.go_profile.import_namespace = "go";        // legacy era...
  module.go_profile.producers_version = "go1.26.5"; // ...modern version
  module.go_profile.pclntab_wire = "F0";
  module.resolve_go_profile();
  CHECK(module.go_profile.profile == go_profile_t::unknown);
  CHECK(module.go_profile.mismatch_reason.rfind("go profile mismatch:", 0) == 0);
  CHECK(module.go_profile.mismatch_reason.find("go1.26.5") != std::string::npos);

  // No evidence at all is unknown too (never guessed).
  module_t bare;
  bare.is_go = true;
  bare.resolve_go_profile();
  CHECK(bare.go_profile.profile == go_profile_t::unknown);
  CHECK(bare.go_profile.mismatch_reason.rfind("go profile mismatch:", 0) == 0);
}

TEST_CASE("G0: corpus ground truth — classification is self-consistent, non-Go stays none")
{
  // The pinned family constants live in the unit tests above and the
  // in-repo sourced fixtures (G12, go1.27.4); on the dev corpus we assert
  // the three-layer resolver's own consistency: the producers version it
  // recovered must map to exactly the family it resolved, with no mismatch
  // diagnostic — no corpus build strings pinned here.
  struct corpus_pin_t
  {
    const module_t *mod;
    bool go;
  };
  const corpus_pin_t corpora[] = {
    { cached_main126(), true },
    { cached_go_module("/wasm-test/gowasm-balls/main.wasm"), true },
  };
  for ( const corpus_pin_t &pin : corpora )
  {
    if ( pin.mod == nullptr )
      continue;
    if ( !pin.go )
      continue;
    int major = -1, minor = -1;
    REQUIRE(parse_go_version_string(pin.mod->go_profile.producers_version.c_str(), &major, &minor));
    CHECK(pin.mod->go_profile.profile == go_profile_for_version(major, minor));
    CHECK(pin.mod->go_profile.profile != go_profile_t::unknown);
    CHECK(pin.mod->go_profile.mismatch_reason.empty());
  }

  // Non-Go corpus stays none (fail-closed, no guesswork).
  module_t duckdb;
  if ( parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/wasm-test/duckdb.wasm",
                         &duckdb) )
    CHECK(duckdb.go_profile.profile == go_profile_t::none);
}

TEST_CASE(
  "M3d/G2: Go function IR built once at load, round-trips blob 'x', and skips legacy/unknown")
{
  if ( const module_t *main126 = cached_main126() )
  {
    // First body with a built IR and at least one call — the same probe
    // class the pinned wasm_func_27 held: shape consistency plus a lossless
    // encode/decode round-trip (arg-collection correctness itself is pinned
    // on the in-repo g3_wb_* fixtures below).
    const body_t *probe = nullptr;
    for ( const body_t &b : main126->bodies )
      if ( b.go_ir.present && !b.go_ir.calls.empty() )
      {
        probe = &b;
        break;
      }
    REQUIRE(probe != nullptr);
    const go_function_ir_t &ir = probe->go_ir;
    CHECK(ir.unwind_exit != UINT32_MAX);
    CHECK(ir.entry_point_loop != UINT32_MAX);
    CHECK(ir.resume_table.dispatch_offset != UINT32_MAX);
    CHECK(ir.resume_table.targets.size() >= 2);
    CHECK(ir.resume_table.targets.size() == ir.resume_table.depths.size());
    CHECK(ir.calls.size() == probe->go_calls.size());
    CHECK(ir.sp_refresh.size() == ir.calls.size());

    // Round-trip encode/decode via blob 'x' v3.
    std::vector<uint32> encoded;
    encode_go_function_ir_blob(ir, &encoded);
    CHECK(!encoded.empty());
    go_function_ir_t decoded;
    REQUIRE(decode_go_function_ir_blob(encoded.data(), encoded.size(), &decoded));
    CHECK(decoded.present);
    CHECK(decoded.unwind_exit == ir.unwind_exit);
    CHECK(decoded.entry_point_loop == ir.entry_point_loop);
    CHECK(decoded.resume_table.dispatch_offset == ir.resume_table.dispatch_offset);
    CHECK(decoded.resume_table.targets == ir.resume_table.targets);
    CHECK(decoded.resume_table.depths == ir.resume_table.depths);
    CHECK(decoded.jmps.size() == ir.jmps.size());
    CHECK(decoded.calls.size() == ir.calls.size());
    CHECK(decoded.sp_refresh.size() == ir.sp_refresh.size());
    CHECK(decoded.morestack == ir.morestack);
    CHECK(decoded.ret == ir.ret);
    CHECK(decoded.pc_b_register_proof == ir.pc_b_register_proof);
    for ( std::size_t i = 0; i < ir.calls.size(); ++i )
      CHECK(decoded.calls[i].args.size() == ir.calls[i].args.size());

    // Demoting profile to go1_19_legacy or unknown skips IR creation.
    module_t small_go;
    REQUIRE(parse_module_file(
      std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/s2_go_sp_stable.wasm", &small_go));
    CHECK(small_go.bodies[3].go_ir.present);
    small_go.go_profile.profile = go_profile_t::go1_19_legacy;
    build_go_function_irs(small_go);
    CHECK_FALSE(small_go.bodies[3].go_ir.present);
    small_go.go_profile.profile = go_profile_t::unknown;
    build_go_function_irs(small_go);
    CHECK_FALSE(small_go.bodies[3].go_ir.present);
  }
}

TEST_CASE("G3: dispatcher replacement gate applies at all sizes and fails closed on invalid IR")
{
  if ( const module_t *main126 = cached_main126() )
  {
    // The largest resume table and the largest const-JMP set in the corpus
    // — the shapes the retired max_table=512 / max_records=64 gates refused.
    // Found by size, not by pinned body indices.
    const body_t *max_arms = nullptr;
    const body_t *max_jmps = nullptr;
    for ( const body_t &b : main126->bodies )
    {
      if ( !b.go_ir.present )
        continue;
      if ( max_arms == nullptr
           || b.go_ir.resume_table.targets.size() > max_arms->go_ir.resume_table.targets.size() )
        max_arms = &b;
      if ( max_jmps == nullptr || b.go_ir.jmps.size() > max_jmps->go_ir.jmps.size() )
        max_jmps = &b;
    }
    REQUIRE(max_arms != nullptr);
    REQUIRE(max_jmps != nullptr);
    CHECK(max_arms->go_ir.resume_table.targets.size() > 512); // past the old table gate
    CHECK(max_jmps->go_ir.jmps.size() > 64);                  // past the old records gate
    CHECK(max_arms->go_ir.unflattening_valid(max_arms->size));
    CHECK(max_jmps->go_ir.unflattening_valid(max_jmps->size));

    // Dispatcher-only Go functions (0 const-JMPs, >=2 resume arms) also
    // qualify so their entry switch(PC_B) is replaced with PC_B == 0.
    go_function_ir_t disp_only = max_arms->go_ir;
    disp_only.jmps.clear();
    CHECK(disp_only.unflattening_valid(max_arms->size));

    // Fail-closed preconditions: missing IR, unprovable PC_B, bad arm
    // target, or out-of-range JMP case_index must reject.
    go_function_ir_t no_pcb = max_arms->go_ir;
    no_pcb.pc_b_register_proof = false;
    CHECK_FALSE(no_pcb.unflattening_valid(max_arms->size));

    go_function_ir_t bad_target = max_arms->go_ir;
    bad_target.resume_table.targets[0] = max_arms->size;
    CHECK_FALSE(bad_target.unflattening_valid(max_arms->size));

    go_function_ir_t bad_jmp = max_arms->go_ir;
    bad_jmp.jmps[0].case_index = uint32(bad_jmp.resume_table.targets.size() - 1);
    CHECK_FALSE(bad_jmp.unflattening_valid(max_arms->size));
  }
}

TEST_CASE(
  "G11: dispatcher replacement covers every size class — the 64/512 gates are gone (todo §5 G11)")
{
  // The census below aggregates over all four Go corpora; a partial corpus
  // would fail the floors for the wrong reason. Absent corpora skip (or
  // fail under WASMHEX_REQUIRE_CORPUS=ON) via the corpus gate.
  WASMHEX_REQUIRE_CORPUS_FILE("/wasm-test/gowasm-balls/main.wasm");
  WASMHEX_REQUIRE_CORPUS_FILE("/wasm-test/gowasm-ballclone/main.wasm");
  WASMHEX_REQUIRE_CORPUS_FILE("/wasm-test/gowasm126/main.wasm");
  WASMHEX_REQUIRE_CORPUS_FILE("/wasm-test/wasm-test/main.wasm");
  struct counts_t
  {
    size_t bodies = 0;
    size_t ir_present = 0;
    size_t unflattenable = 0;
    size_t refused = 0;         // present IR but fail-closed (breakdown below — never size)
    size_t ref_no_pcb = 0;      // local 0 not provable as PC_B-only
    size_t ref_no_dispatch = 0; // no entry dispatcher found
    size_t ref_arms_lt2 = 0;    // fewer than 2 resume arms (no resume points)
    size_t ref_bad_target = 0;  // an arm target outside the body
    size_t ref_bad_jmp = 0;     // a const-JMP case_index hits the default arm
    size_t small = 0;           // arms < 64
    size_t mid = 0;             // 64 <= arms <= 512
    size_t large = 0;           // arms > 512 (old max_table gate)
    size_t jmps_over_64 = 0;    // old max_records gate
    size_t max_arms = 0;
  };
  const struct
  {
    const char *rel;
    const char *label;
  } corpora[] = {
    { "/wasm-test/gowasm-balls/main.wasm", "balls(g1.27.3)" },
    { "/wasm-test/gowasm-ballclone/main.wasm", "ballclone(g1.27.3)" },
    { "/wasm-test/gowasm126/main.wasm", "g126(g1.26.5)" },
    { "/wasm-test/wasm-test/main.wasm", "main126(g1.26.5,strip)" },
  };
  counts_t all;
  const body_t *monster = nullptr; // corpus-wide max-arms body (see below)
  for ( const auto &corpus : corpora )
  {
    const module_t *mod = cached_go_module(corpus.rel);
    if ( mod == nullptr )
    {
      std::printf("G11 census %-20s MISSING (dev corpus absent)\n", corpus.label);
      continue;
    }
    counts_t c;
    for ( const body_t &b : mod->bodies )
    {
      ++c.bodies;
      const go_function_ir_t &ir = b.go_ir;
      if ( !ir.present )
        continue;
      ++c.ir_present;
      if ( !ir.unflattening_valid(b.size) )
      {
        ++c.refused;
        // Mirror of the fail-closed conditions in unflattening_valid
        // (ir.cpp): every bucket is a VALIDITY refusal, not a size one.
        if ( !ir.pc_b_register_proof )
          ++c.ref_no_pcb;
        else if ( ir.resume_table.dispatch_offset == UINT32_MAX )
          ++c.ref_no_dispatch;
        else if ( ir.resume_table.targets.size() < 2
                  || ir.resume_table.targets.size() != ir.resume_table.depths.size() )
          ++c.ref_arms_lt2;
        else if ( std::any_of(ir.resume_table.targets.begin(), ir.resume_table.targets.end(),
                              [&](std::uint32_t t) { return t == 0 || t >= b.size; }) )
          ++c.ref_bad_target;
        else
          ++c.ref_bad_jmp;
        continue;
      }
      ++c.unflattenable;
      const size_t arms = ir.resume_table.targets.size();
      if ( arms < 64 )
        ++c.small;
      else if ( arms <= 512 )
        ++c.mid;
      else
        ++c.large;
      if ( ir.jmps.size() > 64 )
        ++c.jmps_over_64;
      if ( arms > c.max_arms )
      {
        c.max_arms = arms;
        if ( monster == nullptr || arms > monster->go_ir.resume_table.targets.size() )
          monster = &b;
      }
    }
    std::printf(
      "G11 census %-20s bodies=%zu ir=%zu valid=%zu refused=%zu (pcb=%zu nodispatch=%zu armslt2=%zu "
      "badtarget=%zu badjmp=%zu) arms<64=%zu arms64..512=%zu arms>512=%zu jmps>64=%zu max_arms=%zu\n",
      corpus.label, c.bodies, c.ir_present, c.unflattenable, c.refused, c.ref_no_pcb,
      c.ref_no_dispatch, c.ref_arms_lt2, c.ref_bad_target, c.ref_bad_jmp, c.small, c.mid, c.large,
      c.jmps_over_64, c.max_arms);
    all.bodies += c.bodies;
    all.ir_present += c.ir_present;
    all.unflattenable += c.unflattenable;
    all.refused += c.refused;
    all.ref_no_pcb += c.ref_no_pcb;
    all.ref_no_dispatch += c.ref_no_dispatch;
    all.ref_arms_lt2 += c.ref_arms_lt2;
    all.ref_bad_target += c.ref_bad_target;
    all.ref_bad_jmp += c.ref_bad_jmp;
    all.small += c.small;
    all.mid += c.mid;
    all.large += c.large;
    all.jmps_over_64 += c.jmps_over_64;
    if ( c.max_arms > all.max_arms )
      all.max_arms = c.max_arms;
    CHECK(c.unflattenable == c.small + c.mid + c.large);
    CHECK(c.refused
          == c.ref_no_pcb + c.ref_no_dispatch + c.ref_arms_lt2 + c.ref_bad_target + c.ref_bad_jmp);
  }
  // All four corpora present on this machine: every size class non-empty
  // and fully unflattenable (the old 64/512 gates refused exactly these
  // classes; HEAD refuses only on validity, never on size).
  REQUIRE(all.bodies > 0);
  CHECK(all.unflattenable == all.ir_present - all.refused);
  CHECK(all.small > 0);
  CHECK(all.mid > 0);
  CHECK(all.large > 0);
  CHECK(all.jmps_over_64 > 0);
  CHECK(all.max_arms > 512);

  // The documented monster — the corpus-wide max-arms body (a 334 KB
  // package-init function whose 15k-arm resume table was previously capped
  // by both old gates) must be IR-proven unflattenable at HEAD. Found by
  // size through the census above, not by a pinned body index.
  REQUIRE(monster != nullptr);
  CHECK(monster->go_ir.present);
  CHECK(monster->go_ir.resume_table.targets.size() == all.max_arms);
  CHECK(monster->go_ir.resume_table.targets.size() > 512);
  CHECK(monster->go_ir.jmps.size() > 64);
  CHECK(monster->go_ir.unflattening_valid(monster->size));
}

TEST_CASE("G12: five sourced Go fixtures — profile, Go IR, and per-kind pins (todo §5 G12 / §3.10)")
{
  // In-repo sourced fixtures (tests/go/*.go + build.sh, go1.27.4, sha256
  // in tests/go/README.md). A missing fixture is a FAIL, not a skip —
  // that is the whole point of sourced fixtures for the Go side.
  const struct
  {
    const char *fixture;
    const char *target;
  } kinds[] = {
    { "go_loop", "main.sumSquares" }, { "go_slice", "main.sumSlice" }, { "go_map", "main.bump" },
    { "go_iface", "main.areaOf" },    { "go_closure", "main.adder" },
  };
  for ( const auto &kind : kinds )
  {
    module_t mod;
    const std::string path
      = std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/" + kind.fixture + ".wasm";
    REQUIRE_MESSAGE(parse_module_file(path, &mod), "G12 fixture missing/corrupt: " << path);
    CHECK(mod.go_profile.profile == go_profile_t::go1_27_series);
    CHECK(mod.go_profile.producers_version == "go1.27.4");
    CHECK(mod.go_profile.import_namespace == "gojs");

    const std::size_t imp = mod.imported_func_types.size();
    const body_t *target = nullptr;
    for ( std::size_t i = 0; i < mod.bodies.size() && i < mod.pclntab_function_names.size(); ++i )
    {
      if ( mod.pclntab_function_names[i] == kind.target )
      {
        target = &mod.bodies[i];
        break;
      }
    }
    REQUIRE_MESSAGE(target != nullptr, "target function missing from pclntab: " << kind.target);
    CHECK(target->go_ir.present);
    CHECK(target->go_ir.unflattening_valid(target->size));

    // Resolve a callee name through the pclntab (defined functions only).
    const auto callee_name = [&](const go_call_ir_t &call) -> std::string
    {
      if ( call.fn_index < imp || call.fn_index - imp >= mod.pclntab_function_names.size() )
        return std::string();
      return mod.pclntab_function_names[call.fn_index - imp];
    };

    if ( std::string(kind.fixture) == "go_loop" )
    {
      // Counting loop (sha256-pinned fixture → exact pin): the body has
      // exactly 2 const-JMPs (loop back-edge + loop exit) over a 5-arm
      // resume table (entry + morestack/call resume points).
      std::printf("G12 go_loop %s jmps=%zu arms=%zu\n", kind.target, target->go_ir.jmps.size(),
                  target->go_ir.resume_table.targets.size());
      CHECK(target->go_ir.jmps.size() == 2);
      CHECK(target->go_ir.resume_table.targets.size() == 5);
    }
    else if ( std::string(kind.fixture) == "go_map" )
    {
      // map[k] read + m[k] = write over a string key: the faststr pair.
      bool has_access = false, has_assign = false;
      for ( const go_call_ir_t &call : target->go_ir.calls )
      {
        const std::string name = callee_name(call);
        if ( name.find("runtime.mapaccess1_faststr") != std::string::npos )
          has_access = true;
        if ( name.find("runtime.mapassign_faststr") != std::string::npos )
          has_assign = true;
      }
      CHECK(has_access);
      CHECK(has_assign);
    }
    else if ( std::string(kind.fixture) == "go_iface" )
    {
      // itab dispatch: at least one PC_F indirect call inside areaOf.
      size_t indirect = 0;
      for ( const go_call_ir_t &call : target->go_ir.calls )
        if ( call.is_pc_f_indirect )
          ++indirect;
      CHECK(indirect >= 1);
      // Both concrete methods exist next to the target.
      bool square = false, circle = false;
      for ( const std::string &n : mod.pclntab_function_names )
      {
        if ( n == "main.square.area" )
          square = true;
        if ( n == "main.circle.area" )
          circle = true;
      }
      CHECK(square);
      CHECK(circle);
    }
    else if ( std::string(kind.fixture) == "go_closure" )
    {
      // The closure body is a distinct function; adder hands out a funcval.
      bool func1 = false;
      for ( const std::string &n : mod.pclntab_function_names )
        if ( n == "main.adder.func1" )
          func1 = true;
      CHECK(func1);
    }
  }
}

TEST_CASE("G4: PC_F indirect-call protocol gate, constant funcval resolution, and blob round-trip")
{
  if ( const module_t *main126 = cached_main126() )
  {
    // Census over every PC_F call_indirect site: each one passes the
    // protocol gate, and at least one resolves a CONSTANT funcval (rodata
    // slot -> callee) — the properties the pinned wasm_func_3002/3011/3270
    // counts and the wasm_func_725@2072->3271 site held.
    std::size_t pcf_calls = 0;
    std::size_t constant_resolved = 0;
    const go_call_ir_t *proven_site = nullptr;
    const body_t *proven_body = nullptr;
    for ( const body_t &b : main126->bodies )
      for ( const go_call_ir_t &call : b.go_ir.calls )
        if ( call.is_pc_f_indirect )
        {
          ++pcf_calls;
          CHECK(call.pc_f_protocol_valid(b.size));
          if ( call.fn_index != UINT32_MAX )
          {
            ++constant_resolved;
            if ( proven_site == nullptr )
            {
              proven_site = &call;
              proven_body = &b;
            }
          }
        }
    CHECK(pcf_calls > 0);
    CHECK(constant_resolved > 0);
    REQUIRE(proven_site != nullptr);
    // A constant-resolved site names a real defined callee in this module.
    CHECK(proven_site->fn_index >= main126->imported_func_types.size());
    CHECK(proven_site->fn_index < main126->imported_func_types.size() + main126->bodies.size());
    CHECK(proven_site->pc_f_protocol_valid(proven_body->size));

    // Round-trip encode/decode of a function with PC_F indirect calls (the
    // first such body — no pinned index).
    const body_t *pcf_body = nullptr;
    for ( const body_t &b : main126->bodies )
      for ( const go_call_ir_t &c : b.go_ir.calls )
        if ( c.is_pc_f_indirect )
        {
          pcf_body = &b;
          break;
        }
    REQUIRE(pcf_body != nullptr);
    std::vector<uint32> encoded;
    encode_go_function_ir_blob(pcf_body->go_ir, &encoded);
    go_function_ir_t decoded;
    REQUIRE(decode_go_function_ir_blob(encoded.data(), encoded.size(), &decoded));
    REQUIRE(decoded.calls.size() == pcf_body->go_ir.calls.size());
    size_t pcf_decoded = 0;
    for ( size_t i = 0; i < decoded.calls.size(); ++i )
    {
      CHECK(decoded.calls[i].is_pc_f_indirect == pcf_body->go_ir.calls[i].is_pc_f_indirect);
      if ( decoded.calls[i].is_pc_f_indirect )
      {
        ++pcf_decoded;
        CHECK(decoded.calls[i].pc_f_protocol_valid(pcf_body->size));
        for ( int k = 0; k < 5; ++k )
          CHECK(decoded.calls[i].pc_f_extract_offsets[k]
                == pcf_body->go_ir.calls[i].pc_f_extract_offsets[k]);
      }
    }
    CHECK(pcf_decoded > 0);

    // Fail-closed gate checks on corrupted PC_F extraction offsets.
    go_call_ir_t bad_call = *proven_site;
    bad_call.pc_f_extract_offsets[2] = UINT32_MAX;
    CHECK_FALSE(bad_call.pc_f_protocol_valid(proven_body->size));
    bad_call = *proven_site;
    bad_call.pc_f_extract_offsets[4] = bad_call.call_offset;
    CHECK_FALSE(bad_call.pc_f_protocol_valid(proven_body->size));
  }
}

TEST_CASE("G3/G4: post-write-barrier stores after block end are not misclassified as call args")
{
  // Sub-shape A fixture: global constant-address pointer store after
  // write-barrier `end` followed by a 1-arg Go call.
  {
    module_t wb_global;
    REQUIRE(parse_module_file(std::string(WASMHEX_REPO_DIR)
                                + "/tests/fixtures/g3_wb_global_store_before_call.wasm",
                              &wb_global));
    REQUIRE(wb_global.bodies.size() == 4);
    const body_t &b = wb_global.bodies[3];
    CHECK(b.go_ir.present);
    CHECK(b.go_ir.unflattening_valid(b.size));
    REQUIRE(b.go_ir.calls.size() == 1);
    // Only the true `[SP + 0] = 77` store after the global store is collected;
    // the post-write-barrier `*(i64*)1024 = val` store does not cross `end`.
    CHECK(b.go_ir.calls[0].args.size() == 1);
    REQUIRE(b.go_calls.size() == 1);
    CHECK(b.go_calls[0].args.size() == 1);
  }

  // Sub-shape B fixture: local pointer heap store after write-barrier `end`
  // followed by a 0-arg Go call.
  {
    module_t wb_local;
    REQUIRE(parse_module_file(std::string(WASMHEX_REPO_DIR)
                                + "/tests/fixtures/g3_wb_local_ptr_store_before_call.wasm",
                              &wb_local));
    REQUIRE(wb_local.bodies.size() == 4);
    const body_t &b = wb_local.bodies[3];
    CHECK(b.go_ir.present);
    CHECK(b.go_ir.unflattening_valid(b.size));
    REQUIRE(b.go_ir.calls.size() == 1);
    // The 0-arg Go call keeps 0 args; `*ptr = 0` after `end` is preserved.
    CHECK(b.go_ir.calls[0].args.empty());
    REQUIRE(b.go_calls.size() == 1);
    CHECK(b.go_calls[0].args.empty());
  }

  // Corpus verification: the post-write-barrier store regress class is now
  // swept corpus-wide — a collected argument store always sits before its
  // call site (the store at the formerly failing `end` offsets is never
  // claimed by the subsequent Go call), on every body of main.wasm.
  if ( const module_t *main126 = cached_main126() )
  {
    std::size_t checked_calls = 0, checked_args = 0;
    for ( const body_t &b : main126->bodies )
      for ( const go_call_ir_t &c : b.go_ir.calls )
      {
        ++checked_calls;
        for ( const go_arg_ir_t &a : c.args )
        {
          CHECK(a.store_offset < c.call_offset);
          ++checked_args;
        }
      }
    CHECK(checked_calls > 0);
    CHECK(checked_args > 0);
  }
}

TEST_CASE("G6: gcWriteBarrier{1..8} name/body recognizers and corpus ground truth")
{
  // The recognizers moved from go_wb_detail (rewriter_go.cpp) to
  // wasmhex::loader (ir.cpp) — the loader owns callee identity.
  using wasmhex::loader::match_gc_write_barrier_core_body;
  using wasmhex::loader::match_gc_write_barrier_name;
  using wasmhex::loader::match_gc_write_barrier_wrapper_body;
  using wasmhex::loader::match_sigpanic_name;

  // 1. Exact pclntab name recognizer for runtime.gcWriteBarrier{1..8} and runtime.sigpanic.
  std::uint32_t arity = 0;
  for ( std::uint32_t k = 1; k <= 8; ++k )
  {
    const std::string name = "runtime.gcWriteBarrier" + std::to_string(k);
    CHECK(match_gc_write_barrier_name(name.c_str(), &arity));
    CHECK(arity == k);
  }
  CHECK_FALSE(match_gc_write_barrier_name(nullptr, &arity));
  CHECK_FALSE(match_gc_write_barrier_name("runtime.gcWriteBarrier", &arity));
  CHECK_FALSE(match_gc_write_barrier_name("runtime.gcWriteBarrier0", &arity));
  CHECK_FALSE(match_gc_write_barrier_name("runtime.gcWriteBarrier9", &arity));
  CHECK_FALSE(match_gc_write_barrier_name("runtime.gcWriteBarrier2_extra", &arity));
  CHECK_FALSE(match_gc_write_barrier_name("runtime.morestack", &arity));

  CHECK(match_sigpanic_name("runtime.sigpanic"));
  CHECK_FALSE(match_sigpanic_name(nullptr));
  CHECK_FALSE(match_sigpanic_name(""));
  CHECK_FALSE(match_sigpanic_name("runtime.gopanic"));
  CHECK_FALSE(match_sigpanic_name("wasm_func_671"));

  // 2. reproducer fixtures (g6_wb_2slot.wasm and g6_wb_1slot.wasm):
  // verify pclntab recovery of runtime.gcWriteBarrier{2,1} and Go IR validity.
  {
    module_t wb2_mod;
    REQUIRE(parse_module_file(std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/g6_wb_2slot.wasm",
                              &wb2_mod));
    CHECK(wb2_mod.is_go);
    // The fixture grew a 6th defined body (the bytecode-proven
    // gcWriteBarrier core) — the pclntab still names the original five.
    REQUIRE(wb2_mod.pclntab_function_names.size() == 6);
    CHECK(wb2_mod.pclntab_function_names[0] == "runtime.gcWriteBarrier2");
    CHECK(wb2_mod.pclntab_function_names[5].empty());
    CHECK(match_gc_write_barrier_name(wb2_mod.pclntab_function_names[0].c_str(), &arity));
    CHECK(arity == 2u);
    CHECK(wb2_mod.bodies[3].go_ir.unflattening_valid(wb2_mod.bodies[3].size));
    CHECK(wb2_mod.bodies[4].go_ir.unflattening_valid(wb2_mod.bodies[4].size));
  }
  {
    module_t wb1_mod;
    REQUIRE(parse_module_file(std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/g6_wb_1slot.wasm",
                              &wb1_mod));
    CHECK(wb1_mod.is_go);
    REQUIRE(wb1_mod.pclntab_function_names.size() == 6);
    CHECK(wb1_mod.pclntab_function_names[0] == "runtime.gcWriteBarrier1");
    CHECK(wb1_mod.pclntab_function_names[5].empty());
    CHECK(match_gc_write_barrier_name(wb1_mod.pclntab_function_names[0].c_str(), &arity));
    CHECK(arity == 1u);
    CHECK(wb1_mod.bodies[3].go_ir.unflattening_valid(wb1_mod.bodies[3].size));
    CHECK(wb1_mod.bodies[4].go_ir.unflattening_valid(wb1_mod.bodies[4].size));
  }

  // 3. Corpus census (name-agnostic — works on the stripped module): the
  //    arity-1..8 wrapper family is found by bytecode, every wrapper names
  //    a core body, and that core body matches the core recognizer. The
  //    wrapper count 8 is the gcWriteBarrier{1..8} ABI family, not a
  //    corpus fingerprint.
  if ( const module_t *main126 = cached_main126() )
  {
    std::size_t wrappers = 0;
    std::set<std::uint32_t> cores;
    std::uint32_t parsed_arity = 0;
    std::uint32_t core_idx = 0;
    for ( const body_t &wb_body : main126->bodies )
    {
      if ( !match_gc_write_barrier_wrapper_body(main126->code.data() + wb_body.offset, wb_body.size,
                                                &parsed_arity, &core_idx) )
        continue;
      ++wrappers;
      CHECK(parsed_arity >= 1);
      CHECK(parsed_arity <= 8);
      CHECK(core_idx < main126->imported_func_types.size() + main126->bodies.size());
      cores.insert(core_idx);
    }
    CHECK(wrappers == 8);
    REQUIRE(cores.size() == 1);
    const std::uint32_t core_fn = *cores.begin();
    REQUIRE(core_fn >= main126->imported_func_types.size());
    const body_t &core_body = main126->bodies[core_fn - main126->imported_func_types.size()];
    CHECK(
      match_gc_write_barrier_core_body(main126->code.data() + core_body.offset, core_body.size));

    // Near-miss: non-multiple-of-8 byte count (12) or extra instruction must fail.
    const std::uint8_t bad_bytes_12[] = { 0x42, 0x0C, 0x10, 0xB2, 0x0A, 0x0F, 0x00, 0x0B };
    CHECK_FALSE(match_gc_write_barrier_wrapper_body(bad_bytes_12, sizeof(bad_bytes_12),
                                                    &parsed_arity, &core_idx));
    const std::uint8_t bad_bytes_72[] = { 0x42, 0xC8, 0x00, 0x10, 0xB2, 0x0A, 0x0F, 0x00, 0x0B };
    CHECK_FALSE(match_gc_write_barrier_wrapper_body(bad_bytes_72, sizeof(bad_bytes_72),
                                                    &parsed_arity, &core_idx));
  }
}

TEST_CASE("G6: structural sigpanic-guard gate — stripped-module fixture and corpus ground truth")
{
  if ( const module_t *main126 = cached_main126() )
  {
    // Corpus census: terminal-noreturn bodies exist (the sigpanic-class
    // guards the pinned wasm_func_671 held) and every one of them really
    // returns nothing — a body with a recorded `return` never qualifies
    // (the wasm_func_1301 near-miss class, swept corpus-wide).
    std::size_t qualifying = 0;
    std::size_t returning = 0;
    for ( const body_t &b : main126->bodies )
    {
      if ( !b.go_ir.present )
        continue;
      if ( is_go_terminal_noreturn_function(b.go_ir, false) )
      {
        ++qualifying;
        CHECK(b.go_ir.ret.empty()); // a returning body never qualifies
      }
      if ( !b.go_ir.ret.empty() )
        ++returning;
    }
    CHECK(qualifying > 0);
    CHECK(returning > 0); // the near-miss class is non-empty too
  }

  // Reproducer fixture: a fully stripped Go module (no pclntab — every
  // function a synthetic `wasm_func_N`) with the bytecode-proven
  // gcWriteBarrier2 wrapper+core pair, a structurally terminal guard helper,
  // and a near-miss loop helper. The positive write barrier collapses to the
  // post-if guard + `// go: write barrier`; the near-miss fails closed.
  {
    module_t mod;
    REQUIRE(parse_module_file(
      std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/g6_wb_structural_guard.wasm", &mod));
    CHECK(mod.is_go);
    CHECK(mod.go_provenance);
    CHECK(mod.pclntab_function_names.empty()); // fully stripped
    REQUIRE(mod.bodies.size() == 6);

    const body_t &wrapper = mod.bodies[1];
    std::uint32_t arity = 0;
    std::uint32_t core_idx = 0;
    REQUIRE(wasmhex::loader::match_gc_write_barrier_wrapper_body(mod.code.data() + wrapper.offset,
                                                                 wrapper.size, &arity, &core_idx));
    CHECK(arity == 2u);
    CHECK(core_idx == 1u);

    // body 0 (fn 1): gcWriteBarrier core — wbBuf bump-allocation loop prologue.
    const body_t &core = mod.bodies[0];
    CHECK(
      wasmhex::loader::match_gc_write_barrier_core_body(mod.code.data() + core.offset, core.size));

    // body 2 (fn 3): terminal guard helper — valid IR, zero 0x0F, forward JMP.
    const go_function_ir_t &term_ir = mod.bodies[2].go_ir;
    CHECK(term_ir.present);
    CHECK(term_ir.unwind_exit != UINT32_MAX);
    CHECK(term_ir.pc_b_register_proof);
    CHECK(term_ir.ret.empty());
    CHECK(is_go_terminal_noreturn_function(term_ir, false));

    // body 3 (fn 4): near-miss loop helper — zero 0x0F but a backward JMP.
    const go_function_ir_t &loop_ir = mod.bodies[3].go_ir;
    CHECK(loop_ir.present);
    CHECK(loop_ir.ret.empty());
    CHECK_FALSE(is_go_terminal_noreturn_function(loop_ir, false));

    CHECK(mod.bodies[4].go_ir.unflattening_valid(mod.bodies[4].size));
    CHECK(mod.bodies[5].go_ir.unflattening_valid(mod.bodies[5].size));
  }
}

TEST_CASE("G6/M4-D4: write-barrier sites proven at the IR layer (GOWB tail)")
{
  // The ctree shape verifier (wb_candidate_t/classify_wb_candidate) was
  // deleted with the move: the loader now proves the whole protocol
  // per site (collect_go_wb_evidence) and the ctree layer consumes marks.
  // This pins the proof from the loader side: corpus census floors, blob
  // round-trip, and the fail-closed validity bounds.

  // 1. wb_site_valid near-misses (pure struct contract).
  {
    go_wb_site_ir_t bad{};
    CHECK_FALSE(bad.wb_site_valid(1024)); // everything unproven
    bad.guard_if_offset = 10;
    bad.call_offset = 40;
    bad.arity = 2;
    bad.buf_local = 2;
    bad.spill_count = 3;
    CHECK(bad.wb_site_valid(1024));
    CHECK_FALSE(bad.wb_site_valid(40)); // call past the body end
    bad.arity = 0;
    CHECK_FALSE(bad.wb_site_valid(1024)); // arity out of 1..8
    bad.arity = 9;
    CHECK_FALSE(bad.wb_site_valid(1024));
    bad.arity = 2;
    bad.guard_if_offset = 50; // guard after the call
    CHECK_FALSE(bad.wb_site_valid(1024));
    // An empty spill region is legal — the compiler may write the
    // argument window before the guard (main126 wasm_func_1557 shape).
    bad.guard_if_offset = 10;
    bad.spill_count = 0;
    CHECK(bad.wb_site_valid(1024));
    bad.buf_local = UINT32_MAX; // missing buf evidence still fails
    CHECK_FALSE(bad.wb_site_valid(1024));
  }

  if ( const module_t *balls = cached_go_module("/wasm-test/gowasm-balls/main.wasm") )
  {
    std::size_t bodies_with_wb = 0;
    std::size_t sites = 0;
    for ( const body_t &b : balls->bodies )
    {
      if ( b.go_ir.wb_sites.empty() )
        continue;
      ++bodies_with_wb;
      for ( const go_wb_site_ir_t &site : b.go_ir.wb_sites )
      {
        ++sites;
        CHECK(site.arity >= 1);
        CHECK(site.arity <= 8);
        CHECK(site.wb_site_valid(b.size));
        CHECK(site.guard_if_offset < site.call_offset);
      }
    }
    CHECK(bodies_with_wb > 0);     // real Go corpora carry WB sites
    CHECK(sites > bodies_with_wb); // many sites per body (multi-slot spills)
  }

  // 3. Blob round-trip keeps every wb-site field; truncation fails closed.
  if ( const module_t *balls = cached_go_module("/wasm-test/gowasm-balls/main.wasm") )
  {
    const go_function_ir_t *with_wb = nullptr;
    for ( const body_t &b : balls->bodies )
      if ( !b.go_ir.wb_sites.empty() )
      {
        with_wb = &b.go_ir;
        break;
      }
    REQUIRE(with_wb != nullptr);
    std::vector<uint32> encoded;
    encode_go_function_ir_blob(*with_wb, &encoded);
    go_function_ir_t decoded;
    REQUIRE(decode_go_function_ir_blob(encoded.data(), encoded.size(), &decoded));
    REQUIRE(decoded.wb_sites.size() == with_wb->wb_sites.size());
    for ( std::size_t i = 0; i < with_wb->wb_sites.size(); ++i )
    {
      CHECK(decoded.wb_sites[i].guard_if_offset == with_wb->wb_sites[i].guard_if_offset);
      CHECK(decoded.wb_sites[i].call_offset == with_wb->wb_sites[i].call_offset);
      CHECK(decoded.wb_sites[i].arity == with_wb->wb_sites[i].arity);
      CHECK(decoded.wb_sites[i].buf_local == with_wb->wb_sites[i].buf_local);
      CHECK(decoded.wb_sites[i].spill_count == with_wb->wb_sites[i].spill_count);
      CHECK(decoded.wb_sites[i].has_in_body_nil_guard
            == with_wb->wb_sites[i].has_in_body_nil_guard);
      CHECK(decoded.wb_sites[i].has_old_value_chain == with_wb->wb_sites[i].has_old_value_chain);
    }
    // Truncated inside the GOWB tail: the whole blob is rejected (fail
    // closed) exactly like the other versioned tails.
    CHECK_FALSE(decode_go_function_ir_blob(encoded.data(), encoded.size() - 1, &decoded));
  }
}

TEST_CASE(
  "G6-runtime: runtime.systemstack fast-path and wasm_pc_f_loop IR, round-trip, and corpus ground truth")
{
  // 1. Reproducer fixture: g6_runtime_systemstack.wasm
  {
    module_t sys_mod;
    REQUIRE(parse_module_file(
      std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/g6_runtime_systemstack.wasm", &sys_mod));
    CHECK(sys_mod.is_go);
    CHECK(sys_mod.go_provenance);
    REQUIRE(sys_mod.bodies.size() == 3);
    const body_t &sys_body = sys_mod.bodies[2];
    const go_function_ir_t &ir = sys_body.go_ir;
    CHECK(ir.present);
    CHECK(ir.unflattening_valid(sys_body.size));
    CHECK_FALSE(ir.is_pc_f_loop);
    REQUIRE(ir.calls.size() == 2);
    REQUIRE(ir.sp_refresh.size() == 2);
    // call[0] is the slow-path g0 PC_F indirect call; call[1] is the fast-path return call.
    CHECK(ir.calls[0].is_pc_f_indirect);
    CHECK_FALSE(ir.calls[0].is_pc_f_fast_return);
    CHECK(ir.calls[0].pc_f_protocol_valid(sys_body.size));
    CHECK(ir.calls[1].is_pc_f_indirect);
    CHECK(ir.calls[1].is_pc_f_fast_return);
    CHECK(ir.calls[1].pc_f_protocol_valid(sys_body.size));

    // Blob 'x' v3 round-trip preserves both standard and fast-path PC_F calls.
    std::vector<uint32> encoded;
    encode_go_function_ir_blob(ir, &encoded);
    go_function_ir_t decoded;
    REQUIRE(decode_go_function_ir_blob(encoded.data(), encoded.size(), &decoded));
    REQUIRE(decoded.calls.size() == 2);
    CHECK_FALSE(decoded.calls[0].is_pc_f_fast_return);
    CHECK(decoded.calls[1].is_pc_f_indirect);
    CHECK(decoded.calls[1].is_pc_f_fast_return);
    CHECK(decoded.calls[1].pc_f_protocol_valid(sys_body.size));

    // Fail-closed gate checks on fast-path PC_F call record.
    go_call_ir_t bad_fast = ir.calls[1];
    bad_fast.store_offset = 10;
    CHECK_FALSE(bad_fast.pc_f_protocol_valid(sys_body.size));
    bad_fast = ir.calls[1];
    bad_fast.sp_arith_offsets[0] = 10;
    CHECK_FALSE(bad_fast.pc_f_protocol_valid(sys_body.size));
  }

  // 2. Reproducer fixture: g6_runtime_pc_f_loop.wasm
  {
    module_t loop_mod;
    REQUIRE(parse_module_file(
      std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/g6_runtime_pc_f_loop.wasm", &loop_mod));
    CHECK(loop_mod.is_go);
    CHECK(loop_mod.go_provenance);
    REQUIRE(loop_mod.bodies.size() == 4);
    const body_t &loop_body = loop_mod.bodies[3];
    CHECK(loop_body.size == 41);
    const go_function_ir_t &ir = loop_body.go_ir;
    CHECK(ir.present);
    CHECK(ir.is_pc_f_loop);
    CHECK(ir.pc_f_loop_call_offset == 23u);
    CHECK(ir.pc_f_loop_valid(loop_body.size));

    // Blob 'x' v3 + GOLP tail round-trip.
    std::vector<uint32> encoded;
    encode_go_function_ir_blob(ir, &encoded);
    go_function_ir_t decoded;
    REQUIRE(decode_go_function_ir_blob(encoded.data(), encoded.size(), &decoded));
    CHECK(decoded.present);
    CHECK(decoded.is_pc_f_loop);
    CHECK(decoded.pc_f_loop_call_offset == 23u);
    CHECK(decoded.pc_f_loop_valid(loop_body.size));

    // Fail-closed gate check on out-of-bounds call_offset.
    go_function_ir_t bad_loop = decoded;
    bad_loop.pc_f_loop_call_offset = loop_body.size;
    CHECK_FALSE(bad_loop.pc_f_loop_valid(loop_body.size));
  }

  // 3. Corpus census: every fast-path PC_F call and every wasm_pc_f_loop
  //    body anywhere in the corpus passes its protocol gate — the exact
  //    systemstack/pc_f_loop shapes the pinned body indices held, swept
  //    instead of counted.
  auto runtime_shape_census = [](const module_t &mod)
  {
    std::size_t fast_ret = 0, loops = 0;
    for ( const body_t &b : mod.bodies )
    {
      for ( const go_call_ir_t &c : b.go_ir.calls )
        if ( c.is_pc_f_fast_return )
        {
          ++fast_ret;
          CHECK(c.pc_f_protocol_valid(b.size));
        }
      if ( b.go_ir.is_pc_f_loop )
      {
        ++loops;
        CHECK(b.go_ir.pc_f_loop_valid(b.size));
      }
    }
    CHECK(fast_ret > 0); // the runtime.systemstack fast path exists
    CHECK(loops > 0);    // the wasm_pc_f_loop trampoline exists
  };

  if ( const module_t *main126 = cached_main126() )
    runtime_shape_census(*main126);

  module_t balls;
  if ( parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                         &balls) )
    runtime_shape_census(balls);
}

TEST_CASE("G5: Go frame evidence — framesize, param window, result slots (gowasm-balls)")
{
  WASMHEX_REQUIRE_CORPUS_FILE("/wasm-test/gowasm-balls/main.wasm");
  module_t balls;
  REQUIRE(
    parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm", &balls));
  CHECK(balls.imported_func_types.size() > 0);

  // Census (the structural restatement of what the per-function ground
  // truths pinned): every proven frame passes the gate, the result classes
  // partition the proven set, and every result sits past the param window.
  std::size_t frame_proven = 0, zero_results = 0, one_result = 0, multi_results = 0;
  const go_function_ir_t *probe = nullptr;
  const body_t *probe_body = nullptr;
  for ( const body_t &b : balls.bodies )
  {
    const go_function_ir_t &ir = b.go_ir;
    if ( !ir.present || ir.frame_size == UINT32_MAX )
      continue;
    ++frame_proven;
    CHECK(ir.frame_valid(b.size));
    for ( const go_result_ir_t &result : ir.results )
      CHECK(result.sp_offset >= ir.param_window);
    if ( probe == nullptr && !ir.results.empty() && ir.param_window > 0 )
    {
      probe = &ir;
      probe_body = &b;
    }
    if ( ir.results.empty() )
      ++zero_results;
    else if ( ir.results.size() == 1 )
      ++one_result;
    else
      ++multi_results;
  }
  CHECK(frame_proven > 0);
  CHECK(zero_results > 0); // proven-void bodies exist
  CHECK(one_result > 0);
  CHECK(multi_results > 0); // interface/pair results are recognized too
  CHECK(frame_proven == zero_results + one_result + multi_results);
  REQUIRE(probe != nullptr); // a body with results AND a visible param window

  // Blob 'x' GOFR v1 round-trip on the dynamic probe: encode preserves the
  // frame evidence, and a v3 blob truncated before GOFR still decodes
  // (frame unknown) — the earlier reader/writer contract.
  {
    std::vector<uint32> encoded;
    encode_go_function_ir_blob(*probe, &encoded);
    CHECK(!encoded.empty());
    go_function_ir_t decoded;
    REQUIRE(decode_go_function_ir_blob(encoded.data(), encoded.size(), &decoded));
    CHECK(decoded.frame_size == probe->frame_size);
    CHECK(decoded.param_window == probe->param_window);
    REQUIRE(decoded.results.size() == probe->results.size());
    CHECK(decoded.results[0].opcode == probe->results[0].opcode);
    CHECK(decoded.results[0].sp_offset == probe->results[0].sp_offset);
    CHECK(decoded.results[0].store_offset == probe->results[0].store_offset);
    CHECK(decoded.frame_valid(probe_body->size));

    // Truncate at the GOFR magic (0x474F4652): legacy decode keeps working
    // with the frame unproven.
    std::size_t gofr_at = encoded.size();
    for ( std::size_t i = encoded.size() >= 2 ? encoded.size() - 2 : 0; i-- > 0; )
    {
      if ( encoded[i] == 0x474F4652u )
      {
        gofr_at = i;
        break;
      }
    }
    REQUIRE(gofr_at < encoded.size());
    encoded.resize(gofr_at);
    go_function_ir_t legacy;
    REQUIRE(decode_go_function_ir_blob(encoded.data(), encoded.size(), &legacy));
    CHECK(legacy.present);
    CHECK(legacy.frame_size == UINT32_MAX);
    CHECK(legacy.param_window == 0u);
    CHECK(legacy.results.empty());
    CHECK_FALSE(legacy.frame_valid(probe_body->size));
  }

  // frame_valid fail-closed preconditions on hand-mutated IRs (same probe).
  {
    const uint32 size = probe_body->size;
    CHECK(probe->frame_valid(size));

    go_function_ir_t absent = *probe;
    absent.present = false;
    CHECK_FALSE(absent.frame_valid(size));

    go_function_ir_t no_frame = *probe;
    no_frame.frame_size = UINT32_MAX;
    CHECK_FALSE(no_frame.frame_valid(size));

    go_function_ir_t no_dispatch = *probe;
    no_dispatch.resume_table.dispatch_offset = UINT32_MAX;
    CHECK_FALSE(no_dispatch.frame_valid(size));

    go_function_ir_t in_param = *probe;
    REQUIRE(!in_param.results.empty());
    in_param.results[0].sp_offset = in_param.param_window - 1u;
    CHECK_FALSE(in_param.frame_valid(size));

    go_function_ir_t out_of_body = *probe;
    out_of_body.results[0].store_offset = size;
    CHECK_FALSE(out_of_body.frame_valid(size));

    go_function_ir_t bad_opcode = *probe;
    bad_opcode.results[0].opcode = 0x00;
    CHECK_FALSE(bad_opcode.frame_valid(size));
  }
}

namespace
{
// Count the caller-cross-check verdicts over a corpus.
void count_proven_results(const module_t &mod, std::size_t *single, std::size_t *multi,
                          std::size_t *records)
{
  *single = *multi = *records = 0;
  for ( const body_t &b : mod.bodies )
  {
    if ( !b.go_ir.results_caller_proven() )
      continue;
    std::vector<unsigned char> types;
    if ( !go_result_slot_types(b.go_ir, &types) )
      continue;
    ++*records;
    if ( types.size() == 1 )
      ++*single;
    else
      ++*multi;
  }
}
} // namespace

TEST_CASE("G5-S2b: caller cross-check — proven Go result slots (corpora + ground truth)")
{
  // go_result_slot_types: dedupe by (offset, width), refuse overlaps, map
  // store opcodes to display types.
  {
    go_function_ir_t ir;
    ir.present = true;
    // Two return sites of one i64 slot + one bool store8 slot.
    go_result_ir_t a;
    a.opcode = 0x37;
    a.sp_offset = 8;
    go_result_ir_t a2 = a; // same slot, second `return` site
    go_result_ir_t b;
    b.opcode = 0x3C; // i64.store8 (bool)
    b.sp_offset = 16;
    ir.results = { a, b, a2 };
    std::vector<unsigned char> types;
    REQUIRE(go_result_slot_types(ir, &types));
    REQUIRE(types.size() == 2);
    CHECK(types[0] == 0x7E); // i64.store -> i64
    CHECK(types[1] == 0x7F); // 1-byte bool -> i32

    // Ambiguous overlap: distinct slots sharing bytes are refused.
    go_result_ir_t wide;
    wide.opcode = 0x37;
    wide.sp_offset = 8;
    go_result_ir_t half;
    half.opcode = 0x36; // i32.store
    half.sp_offset = 12;
    ir.results = { wide, half };
    CHECK_FALSE(go_result_slot_types(ir, &types));

    // f32/f64 stores keep float types.
    go_result_ir_t f32s;
    f32s.opcode = 0x38;
    f32s.sp_offset = 0;
    go_result_ir_t f64s;
    f64s.opcode = 0x39;
    f64s.sp_offset = 8;
    ir.results = { f32s, f64s };
    REQUIRE(go_result_slot_types(ir, &types));
    REQUIRE(types.size() == 2);
    CHECK(types[0] == 0x7D);
    CHECK(types[1] == 0x7C);
  }

  // Blob 'x' GOFR v2: the verdict word round-trips; a v1 tail (no verdict
  // word) still decodes with the cross-check unproven.
  {
    WASMHEX_REQUIRE_CORPUS_FILE("/wasm-test/gowasm-balls/main.wasm");
    module_t balls;
    REQUIRE(parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                              &balls));
    std::size_t proven_index = 0;
    for ( std::size_t i = 0; i < balls.bodies.size(); ++i )
      if ( balls.bodies[i].go_ir.results_caller_proven() )
      {
        proven_index = i;
        break;
      }
    const go_function_ir_t &ir = balls.bodies[proven_index].go_ir;
    REQUIRE(ir.results_caller_proven());
    std::vector<uint32> encoded;
    encode_go_function_ir_blob(ir, &encoded);
    go_function_ir_t decoded;
    REQUIRE(decode_go_function_ir_blob(encoded.data(), encoded.size(), &decoded));
    CHECK(decoded.results_caller_proven());

    // Hand-built v1 tail: [magic][1][frame][window][count][records...] with
    // no verdict word decodes as unproven. (The optional GOMS tail rides
    // after GOFR — strip it first when present so the v1 surgery below
    // lands on the frame tail itself.)
    std::vector<uint32> v1 = encoded;
    REQUIRE(v1.size() >= 6);
    for ( std::size_t i = v1.size() >= 2 ? v1.size() - 2 : 0; i-- > 0; )
      if ( v1[i] == 0x474F4D53u ) // "GOMS"
      {
        v1.resize(i);
        break;
      }
    std::size_t gofr_at = v1.size();
    for ( std::size_t i = v1.size() >= 2 ? v1.size() - 2 : 0; i-- > 0; )
      if ( v1[i] == 0x474F4652u )
      {
        gofr_at = i;
        break;
      }
    REQUIRE(gofr_at < v1.size());
    v1[gofr_at + 1] = 1u;     // v1 version word
    v1.resize(v1.size() - 1); // drop the verdict word
    go_function_ir_t legacy;
    REQUIRE(decode_go_function_ir_blob(v1.data(), v1.size(), &legacy));
    CHECK(legacy.frame_size == ir.frame_size);
    REQUIRE(legacy.results.size() == ir.results.size());
    CHECK_FALSE(legacy.results_caller_proven());
  }

  if ( const module_t *main126 = cached_main126() )
  {
    std::size_t single = 0, multi = 0, records = 0;
    count_proven_results(*main126, &single, &multi, &records);
    std::printf("G5-S2b proven main.wasm single=%zu multi=%zu total=%zu\n", single, multi, records);
    CHECK(records > 0);
    CHECK(single > 0);
    CHECK(multi > 0);
    CHECK(records == single + multi);
    // Fail-closed partition: together with the proven records, the
    // no-caller / mismatch / ambiguous states account for EVERY
    // frame-valid result-carrying body, and ambiguity never occurs on a
    // real Go corpus (the cross-check refuses rather than guesses).
    std::size_t no_caller = 0, mismatch = 0, ambiguous = 0, with_records = 0;
    for ( const body_t &b : main126->bodies )
    {
      if ( !b.go_ir.present || !b.go_ir.frame_valid(b.size) || b.go_ir.results.empty() )
        continue;
      ++with_records;
      switch ( b.go_ir.results_caller_state )
      {
        case go_function_ir_t::kResultsNoCaller: ++no_caller; break;
        case go_function_ir_t::kResultsMismatch: ++mismatch; break;
        case go_function_ir_t::kResultsAmbiguous: ++ambiguous; break;
        default: break;
      }
    }
    CHECK(with_records > 0);
    CHECK(with_records == records + no_caller + mismatch + ambiguous);
    CHECK(no_caller > 0); // dynamic-only callees exist on any real corpus
    CHECK(ambiguous == 0u);
  }

  module_t balls;
  if ( parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                         &balls) )
  {
    std::size_t single = 0, multi = 0, records = 0;
    count_proven_results(balls, &single, &multi, &records);
    std::printf("G5-S2b proven balls single=%zu multi=%zu total=%zu\n", single, multi, records);
    CHECK(records > 0);
    CHECK(records == single + multi);

    std::size_t no_caller = 0, mismatch = 0, ambiguous = 0, with_records = 0;
    for ( const body_t &b : balls.bodies )
    {
      if ( !b.go_ir.present || !b.go_ir.frame_valid(b.size) || b.go_ir.results.empty() )
        continue;
      ++with_records;
      switch ( b.go_ir.results_caller_state )
      {
        case go_function_ir_t::kResultsNoCaller: ++no_caller; break;
        case go_function_ir_t::kResultsMismatch: ++mismatch; break;
        case go_function_ir_t::kResultsAmbiguous: ++ambiguous; break;
        default: break;
      }
    }
    CHECK(with_records > 0);
    CHECK(with_records == records + no_caller + mismatch + ambiguous);
    CHECK(no_caller > 0);
    CHECK(ambiguous == 0u);

    // Ground truth by stable symbol names (no body indices): a directly
    // called scalar function is caller-proven; a closure reached only
    // through dynamic PC_F funcvals stays fail-closed.
    const std::size_t intn = body_index_for_name(balls, "math/rand.Intn");
    if ( intn != std::size_t(SIZE_MAX) )
    {
      REQUIRE(balls.bodies[intn].go_ir.present);
      CHECK(balls.bodies[intn].go_ir.results_caller_proven());
      std::vector<unsigned char> intn_types;
      REQUIRE(go_result_slot_types(balls.bodies[intn].go_ir, &intn_types));
      REQUIRE(intn_types.size() == 1);
      CHECK(intn_types[0] == 0x7E); // i64 in / i64 out
    }
    const std::size_t func1 = body_index_for_name(balls, "main.main.func1");
    if ( func1 != std::size_t(SIZE_MAX) )
    {
      REQUIRE(balls.bodies[func1].go_ir.present);
      CHECK_FALSE(balls.bodies[func1].go_ir.results_caller_proven());
    }
    // The cross-check never touches bodies without result records.
    const std::size_t loop = body_index_for_name(balls, "main.loop");
    if ( loop != std::size_t(SIZE_MAX) )
      CHECK_FALSE(balls.bodies[loop].go_ir.results_caller_proven());
  }
}

TEST_CASE("G9: js.Value receiver-global evidence walk — balls corpus")
{
  module_t balls;
  if ( !parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                          &balls) )
    return; // corpus absent on this machine
  std::vector<std::uint64_t> globals;
  REQUIRE(go_jsvalue_receiver_globals(balls, &globals));
  // Fail-closed walk: the package-level js.Value receiver slots are proven
  // (at least one on this corpus), every slot is word-aligned, and none
  // repeats — the exact-address/exact-count pins replaced by the walk's own
  // structural guarantees.
  REQUIRE(!globals.empty());
  for ( std::uint64_t slot : globals )
    CHECK(slot % 8 == 0);
  CHECK(std::set<std::uint64_t>(globals.begin(), globals.end()).size() == globals.size());

  // Non-Go modules prove nothing (empty, not an error).
  module_t plain;
  if ( parse_module_file(std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/add.wasm", &plain) )
  {
    std::vector<std::uint64_t> none;
    REQUIRE(go_jsvalue_receiver_globals(plain, &none));
    CHECK(none.empty());
  }
}

TEST_CASE("G4-lite probe: variadic empty-args call census — balls corpus")
{
  // calls, exactly 4 with an all-zero trailing []any (every slot an
  // i64.const 0 store). The pins are the collector's fail-closed shape
  // denominators.
  module_t balls;
  if ( !parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                          &balls) )
    return; // corpus absent on this machine
  const std::size_t imports = balls.imported_func_types.size();
  std::size_t variadic_calls = 0, empty_shape = 0, nonempty = 0;
  for ( std::size_t b = 0; b < balls.bodies.size(); ++b )
  {
    const go_function_ir_t &ir = balls.bodies[b].go_ir;
    if ( !ir.present )
      continue;
    std::vector<binary::decoded_insn_t> insns;
    for ( const go_call_ir_t &call : ir.calls )
    {
      if ( call.fn_index == UINT32_MAX || call.fn_index < imports )
        continue;
      const std::size_t d = call.fn_index - imports;
      if ( d >= balls.go_types.layouts.size()
           || balls.go_types.layouts[d].source == go_layout_source_t::kNone
           || !balls.go_types.layouts[d].variadic )
        continue;
      ++variadic_calls;
      if ( insns.empty() && !decode_go_body_insns(balls, b, &insns) )
        continue;
      std::size_t zero_stores = 0;
      std::size_t total_stores = 0;
      for ( const go_arg_ir_t &arg : call.args )
      {
        if ( arg.opcode != 0x37 )
          continue;
        ++total_stores;
        for ( std::size_t k = 0; k < insns.size(); ++k )
        {
          if ( insns[k].offset != arg.store_offset || insns[k].opcode != 0x37 )
            continue;
          if ( k >= 1 && insns[k - 1].opcode == 0x42 && insns[k - 1].imm[0] == 0 )
            ++zero_stores; // value = i64.const 0 directly before the store
          break;
        }
      }
      if ( total_stores >= 3 && zero_stores >= 3 )
        ++empty_shape;
      else if ( total_stores > 0 )
        ++nonempty;
    }
  }
  CHECK(variadic_calls > 0);
  // Partition (Go ABI: a variadic call always stores its slice-argument
  // window): every variadic call is either the all-zero []any shape or a
  // non-empty one — computed, so a collector that drops or double-counts a
  // class breaks the sum instead of a pinned triple.
  CHECK(empty_shape + nonempty == variadic_calls);
  CHECK(empty_shape > 0); // the Println()-style all-zero []any exists
  CHECK(nonempty > 0);
}

TEST_CASE("G5 x=f retry (2026-11-16): eligibility census + measured engine decline — balls")
{
  module_t balls;
  if ( !parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                          &balls) )
    return;
  std::size_t eligible = 0;
  std::vector<std::string> names;
  for ( std::size_t b = 0; b < balls.bodies.size(); ++b )
  {
    const go_function_ir_t &ir = balls.bodies[b].go_ir;
    if ( !ir.present )
      continue;
    go_xf_result_info_t info;
    const std::string name = go_body_display_name(balls, b);
    if ( go_xf_single_result(balls, b, name, &info) )
    {
      ++eligible;
      names.push_back(name);
    }
  }
  REQUIRE(eligible > 0);
  // Membership by stable symbol names, order-free: the js.Value family and
  // fmt.Sprintf must all pass the gates (the exact set the count-and-order
  // pin used to hold, without pinning the corpus scale).
  for ( const char *want : { "syscall_js.Value.Get", "syscall_js.Value.Call",
                             "syscall_js.Value.New", "syscall_js.Value.String", "fmt.Sprintf" } )
  {
    bool found = false;
    for ( const std::string &name : names )
      found = found || name == want;
    CHECK_MESSAGE(found, "x=f-eligible function missing: " << want);
  }
}

TEST_CASE("M0/3.1: sret result-slot cross-check — fmt.Sprintf and corpora")
{
  // Hand-built IR: shape params alone (no module) are exercised through
  // the module walks below; here verify the arginfo shape walk parity
  // with collect_go_call_shapes on the Sprintf ground truth.
  module_t balls;
  if ( !parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                          &balls) )
    return; // corpus absent on this machine

  // fmt.Sprintf resolved by its stable pclntab name: raw type 0 (i32)->i32,
  // ArgInfo groups string@0 (16B) + []any@16 (24B), proven result slots
  // [40,48)+[48,56) — the phantom r1 binds the []any group.
  const std::size_t sprintf_body = body_index_for_name(balls, "fmt.Sprintf");
  REQUIRE(sprintf_body != std::size_t(SIZE_MAX));
  REQUIRE(sprintf_body < balls.bodies.size());
  std::vector<unsigned char> sprintf_types;
  REQUIRE(go_result_slot_types(balls.bodies[sprintf_body].go_ir, &sprintf_types));
  CHECK(balls.bodies[sprintf_body].go_ir.results_caller_proven());
  CHECK(go_sret_result_slot_verdict(balls, sprintf_body) == go_sret_slot_verdict_t::kBoundArgument);

  // Corpus verdict census: every verdict is one
  // of the three declared states; kAligned never occurs on real Go (the
  // wasm ABI never passes a result pointer register), kBoundArgument is
  // exactly the set of proven-multi bodies whose ArgInfo shape reaches the
  // phantom slot.
  std::size_t aligned = 0, bound = 0, neutral = 0;
  std::size_t multi_bound = 0, multi_neutral = 0;
  std::size_t proven_total = 0, multi_total = 0;
  for ( std::size_t i = 0; i < balls.bodies.size(); ++i )
  {
    const go_function_ir_t &ir = balls.bodies[i].go_ir;
    std::vector<unsigned char> types;
    if ( !ir.results_caller_proven() || !go_result_slot_types(ir, &types) )
      continue;
    ++proven_total;
    // The loader's forced-sret display (and its downgrade counter) covers
    // exactly the multi-result bodies; single-result bodies never grow a
    // phantom `result` parameter.
    const bool multi = types.size() > 1;
    if ( multi )
      ++multi_total;
    switch ( go_sret_result_slot_verdict(balls, i) )
    {
      case go_sret_slot_verdict_t::kAligned: ++aligned; break;
      case go_sret_slot_verdict_t::kBoundArgument:
        ++bound;
        if ( multi )
          ++multi_bound;
        break;
      case go_sret_slot_verdict_t::kNeutral:
        ++neutral;
        if ( multi )
          ++multi_neutral;
        break;
    }
  }
  std::printf("M0/3.1 balls verdicts aligned=%zu bound=%zu neutral=%zu "
              "(multi: bound=%zu neutral=%zu)\n",
              aligned, bound, neutral, multi_bound, multi_neutral);
  // Computed partition (the pinned verdict triple's structural form):
  // kAligned never occurs on real Go (the wasm ABI never passes a result
  // pointer register), and the two honest verdicts together with the multi
  // split account for every proven body counted in the same loop.
  CHECK(aligned == 0u);
  CHECK(proven_total > 0);
  CHECK(multi_total > 0);
  CHECK(bound + neutral == proven_total);
  CHECK(multi_bound + multi_neutral == multi_total);

  // The synthetic fixture (tests/fixtures/m0a_go_result_slot.wat): a Go
  // callee with the fmt.Sprintf shape (string param + variadic slice +
  // string result) and a caller that reads the result back at the exact
  // window the callee stored — the cross-check must prove both
  // sides, and with no pclntab ArgInfo layer the phantom-slot cross-check
  // stays neutral (nothing binds, the display keeps its form).
  {
    module_t fixture;
    REQUIRE(parse_module_file(
      std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/m0a_go_result_slot.wasm", &fixture));
    REQUIRE(fixture.go_provenance);
    REQUIRE(fixture.bodies.size() == 2);
    const body_t &callee = fixture.bodies[0]; // $sprintf_like
    const go_function_ir_t &ir = callee.go_ir;
    REQUIRE(ir.present);
    REQUIRE(ir.frame_valid(callee.size));
    CHECK(ir.frame_size == 24u);
    // A LOWER bound (params the body never loads are invisible): the
    // scanner proves the window through the slice's len slot; the cap
    // slot load at rel 32 does not extend it in this body.
    CHECK(ir.param_window == 32u);
    REQUIRE(ir.results.size() == 2);
    CHECK(ir.results[0].sp_offset == 40u);
    CHECK(ir.results[1].sp_offset == 48u);
    CHECK(ir.results_caller_proven());
    std::vector<unsigned char> types;
    REQUIRE(go_result_slot_types(ir, &types));
    REQUIRE(types.size() == 2);
    CHECK(types[0] == 0x7E);
    CHECK(types[1] == 0x7E);
    CHECK(go_sret_result_slot_verdict(fixture, 0) == go_sret_slot_verdict_t::kNeutral);
  }
}

TEST_CASE("G10: morestack stack-check prologue proven at the IR layer (todo 3.6)")
{
  module_t balls;
  if ( !parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                          &balls) )
    return; // corpus absent on this machine

  // Resolve a body by its export/pclntab name (mirrors go_body_display_name).
  auto body_for_name = [&balls](const char *want) -> std::size_t
  {
    for ( std::size_t i = 0; i < balls.bodies.size(); ++i )
    {
      std::string name;
      const std::size_t fn_index = balls.imported_func_types.size() + i;
      for ( const export_t &entry : balls.function_names )
        if ( entry.index == fn_index )
        {
          name = entry.name;
          break;
        }
      if ( name.empty() && i < balls.pclntab_function_names.size() )
        name = balls.pclntab_function_names[i];
      if ( name == want )
        return i;
    }
    return std::size_t(SIZE_MAX);
  };

  // main.__Ball_.draw (func[1776]): LARGE frame — the guard compares
  // `SP <= *(g+16) + 64` (192 - 128 StackSmall) and calls
  // runtime.morestack_noctxt.
  {
    const std::size_t draw = body_for_name("main.__Ball_.draw");
    REQUIRE(draw != std::size_t(SIZE_MAX));
    const body_t &b = balls.bodies[draw];
    const go_function_ir_t &ir = b.go_ir;
    REQUIRE(ir.present);
    REQUIRE(ir.prologue.if_offset != UINT32_MAX);
    CHECK(ir.prologue.framesize_extra == 64u);
    CHECK(ir.prologue.guard_const_offset != UINT32_MAX);
    CHECK(ir.prologue.prologue_valid(b.size));
    // The proven call must be a recorded morestack call offset.
    bool call_is_morestack = false;
    for ( std::uint32_t off : ir.morestack )
      if ( off == ir.prologue.call_offset )
        call_is_morestack = true;
    CHECK(call_is_morestack);
    // Evidence ordering on the real bytes: SP read < guard load < guard
    // const < compare < if < resume-PC store < call < br_if < end.
    CHECK(ir.prologue.sp_get_offset < ir.prologue.guard_load_offset);
    CHECK(ir.prologue.guard_const_offset < ir.prologue.compare_offset);
    CHECK(ir.prologue.compare_offset < ir.prologue.if_offset);
    CHECK(ir.prologue.if_offset < ir.prologue.store_offset);
    CHECK(ir.prologue.store_offset < ir.prologue.call_offset);
    CHECK(ir.prologue.call_offset < ir.prologue.br_if_offset);
    CHECK(ir.prologue.br_if_offset < ir.prologue.end_offset);

    // Blob round-trip keeps every prologue field.
    std::vector<uint32> encoded;
    encode_go_function_ir_blob(ir, &encoded);
    go_function_ir_t decoded;
    REQUIRE(decode_go_function_ir_blob(encoded.data(), encoded.size(), &decoded));
    CHECK(decoded.prologue.if_offset == ir.prologue.if_offset);
    CHECK(decoded.prologue.framesize_extra == ir.prologue.framesize_extra);
    CHECK(decoded.prologue.guard_const_offset == ir.prologue.guard_const_offset);
    CHECK(decoded.prologue.sp_get_offset == ir.prologue.sp_get_offset);
    CHECK(decoded.prologue.guard_load_offset == ir.prologue.guard_load_offset);
    CHECK(decoded.prologue.compare_offset == ir.prologue.compare_offset);
    CHECK(decoded.prologue.store_offset == ir.prologue.store_offset);
    CHECK(decoded.prologue.call_offset == ir.prologue.call_offset);
    CHECK(decoded.prologue.br_if_offset == ir.prologue.br_if_offset);
    CHECK(decoded.prologue.end_offset == ir.prologue.end_offset);

    // Fail-closed: a truncated blob inside the GOMS tail must reject the
    // whole decode (never a partial prologue record).
    REQUIRE(encoded.size() > 2);
    go_function_ir_t truncated;
    CHECK_FALSE(decode_go_function_ir_blob(encoded.data(), encoded.size() - 1, &truncated));
    // A pre-GOMS blob (tail cut away) decodes but has no prologue mark —
    // exactly the state a reader sees.
    const std::size_t goms_at = encoded.size() - 12;
    go_function_ir_t pre_g10;
    REQUIRE(decode_go_function_ir_blob(encoded.data(), goms_at, &pre_g10));
    CHECK(pre_g10.prologue.if_offset == UINT32_MAX);
  }

  // main.main.func1 (func[1777]): SMALL frame — the guard compares
  // `SP <= *(g+16)` directly (no const+add leg) and calls
  // runtime.morestack (plain, not the noctxt wrapper).
  {
    const std::size_t f1 = body_for_name("main.main.func1");
    REQUIRE(f1 != std::size_t(SIZE_MAX));
    const body_t &b = balls.bodies[f1];
    const go_function_ir_t &ir = b.go_ir;
    REQUIRE(ir.present);
    REQUIRE(ir.prologue.if_offset != UINT32_MAX);
    CHECK(ir.prologue.framesize_extra == 0u);
    CHECK(ir.prologue.guard_const_offset == UINT32_MAX);
    CHECK(ir.prologue.prologue_valid(b.size));
  }

  // Corpus survey: every function with a recorded morestack call either
  // gets a proven prologue or keeps if_offset at UINT32_MAX (fail-closed
  // skip). This is the hide/skip count the Go rewriter consumes.
  {
    std::size_t ir_present = 0, ms_calls = 0, proven = 0, skipped = 0;
    for ( const body_t &b : balls.bodies )
    {
      if ( !b.go_ir.present )
        continue;
      ++ir_present;
      if ( b.go_ir.morestack.empty() )
        continue;
      ++ms_calls;
      if ( b.go_ir.prologue.if_offset != UINT32_MAX )
      {
        ++proven;
        CHECK(b.go_ir.prologue.prologue_valid(b.size));
      }
      else
      {
        // Shape mismatch: the guard stays VISIBLE (no proof, no hide).
        // The only legal skip is the morestack wrapper itself —
        // runtime.morestack_noctxt has no stack check of its own (its call
        // to runtime.morestack is the tail `call; SP refresh; return`
        // sequence, no `if` guard), so a skip is the correct fail-closed
        // answer exactly there. Verified by symbol name.
        ++skipped;
        CHECK(!b.go_ir.morestack.empty());
        const std::size_t bi = &b - balls.bodies.data();
        CHECK(bi < balls.pclntab_function_names.size());
        if ( bi < balls.pclntab_function_names.size() )
          CHECK(balls.pclntab_function_names[bi] == "runtime.morestack_noctxt");
      }
    }
    std::printf("G10 survey gowasm-balls ir=%zu morestack=%zu proven=%zu skipped=%zu\n", ir_present,
                ms_calls, proven, skipped);
    // Ground truth from the disassembly: every balls function with a
    // stack check follows the two proven shapes (large/small frame), so
    // the matcher is expected to reach every recorded morestack call —
    // at corpus scale (proven dominates), not a pinned count.
    CHECK(ms_calls == proven + skipped);
    CHECK(proven > 0u);
    CHECK(proven > skipped); // the wrappers are the rare exception
  }

  // Stripped-module survey (wasm-test main.wasm, go1.26): export names are
  // gone but the structural morestack discovery (pclntab + the
  // 20-byte noctxt-wrapper body match) still feeds the prologue matcher —
  // corpus-level proof that the IR mark does not depend on symbols.
  if ( const module_t *main126 = cached_main126() )
  {
    std::size_t proven = 0, skipped = 0;
    for ( const body_t &b : main126->bodies )
    {
      if ( !b.go_ir.present || b.go_ir.morestack.empty() )
        continue;
      if ( b.go_ir.prologue.if_offset != UINT32_MAX )
      {
        ++proven;
        CHECK(b.go_ir.prologue.prologue_valid(b.size));
      }
      else
      {
        // Same skip class as balls (no symbols to verify it by name here):
        // at most the two morestack wrappers the Go runtime ships
        // (morestack + morestack_noctxt) may lack a stack check.
        ++skipped;
        CHECK(!b.go_ir.morestack.empty());
      }
    }
    std::printf("G10 survey main126(stripped) proven=%zu skipped=%zu\n", proven, skipped);
    CHECK(proven > 0u);
    CHECK(proven > skipped); // at most the morestack wrappers themselves
  }
}

TEST_CASE("D2-P2: x=f single-result eligibility and typed receivers — balls pins")
{
  module_t balls;
  if ( !parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                          &balls) )
    return; // corpus absent on this machine

  // x=f eligibility (go_xf_single_result): exactly five
  // balls functions pass every gate (read-coverage + single named composite
  // result + shape + no non-frame caller). fmt.Sprintf is NOT among them —
  // the verdict is kBoundArgument (the variadic []any group binds the
  // phantom slot in the retired register-pointer model), so it keeps the
  // sret prototype through the sret-keep bucket.
  std::size_t eligible = 0;
  bool sprintf_eligible = false;
  go_xf_result_info_t info = {};
  const std::size_t sprintf_body = body_index_for_name(balls, "fmt.Sprintf");
  REQUIRE(sprintf_body != std::size_t(SIZE_MAX)); // fmt.Sprintf is corpus-stable
  std::size_t draw_body = balls.bodies.size();
  for ( std::size_t i = 0; i < balls.bodies.size(); ++i )
  {
    // Mirror go_body_display_name: export names first, then pclntab.
    std::string name;
    const std::size_t fn_index = balls.imported_func_types.size() + i;
    for ( const export_t &entry : balls.function_names )
      if ( entry.index == fn_index )
      {
        name = entry.name;
        break;
      }
    if ( name.empty() && i < balls.pclntab_function_names.size() )
      name = balls.pclntab_function_names[i];
    if ( name == "main.__Ball_.draw" )
      draw_body = i;
    if ( !go_xf_single_result(balls, i, name, &info) )
      continue;
    ++eligible;
    if ( i == sprintf_body )
    {
      // The apply switch may be off, but the ELIGIBILITY facts are derived
      // from the signature's ABI groups: a 16-byte string + 24-byte []any
      // param window, then the 16-byte string result tiling [40,56).
      sprintf_eligible = true;
      CHECK(info.min_offset == 16u + 24u);
      CHECK(info.extent == info.min_offset + 16u);
      CHECK(info.slot_types == std::vector<unsigned char>({ 0x7E, 0x7E }));
      CHECK(info.c_name == "go__string");
    }
  }
  CHECK(eligible > 0u);
  CHECK(sprintf_eligible);
  // fmt.Sprintf is kBoundArgument YET x=f-eligible: the gates
  // answer different questions — the verdict models the RETIRED
  // register-pointer call lowering; x=f replaces the phantom entirely and
  // requires every caller to be frame-proven (by-value cells never bind).

  // Typed receivers (go_receiver_c_param): draw's gap-cut signature proves
  // a *main.Ball receiver — one 8-byte pointer word (Go pointers are 8
  // bytes on wasm; the body reads color at a2+32/a2+40).
  REQUIRE(draw_body < balls.bodies.size());
  go_named_sig_t sig;
  const std::string draw_name = "main.__Ball_.draw";
  REQUIRE(go_resolve_named_signature(balls, draw_body, draw_name, &sig));
  CHECK(!sig.complete);
  CHECK(sig.receiver == "*main.Ball");
  bool is_pointer = false;
  go_sig_param_t recv;
  REQUIRE(go_receiver_c_param(balls, sig, &is_pointer, &recv));
  CHECK(is_pointer);
  CHECK(recv.size == 8u);
  CHECK(recv.c_name == "go__main_Ball");
  CHECK(recv.display == "main.Ball");
}

TEST_CASE("G5: Go frame evidence corpus coverage — main.wasm and gowasm-balls")
{
  auto survey = [](const module_t &mod, const char *label)
  {
    std::size_t ir_present = 0;
    std::size_t frame_proven = 0;
    std::size_t zero_results = 0;
    std::size_t one_result = 0;
    std::size_t multi_results = 0;
    std::size_t params_visible = 0;
    std::size_t void_display = 0;
    for ( const body_t &b : mod.bodies )
    {
      if ( !b.go_ir.present )
        continue;
      ++ir_present;
      if ( b.go_ir.frame_size == UINT32_MAX )
        continue;
      ++frame_proven;
      if ( !b.go_ir.frame_valid(b.size) )
        continue; // every proven frame must also pass the gate
      if ( b.go_ir.results.empty() )
        ++zero_results;
      else if ( b.go_ir.results.size() == 1 )
        ++one_result;
      else
        ++multi_results;
      if ( b.go_ir.param_window > 0 )
        ++params_visible;
      // Void-display gate (loader/database.cpp go_proven_void_results):
      // frame proven + zero result slots + at least one real `return` site.
      if ( b.go_ir.results.empty() && !b.go_ir.ret.empty() )
        ++void_display;
    }
    std::printf(
      "G5 survey %-14s ir=%zu frame=%zu void=%zu one=%zu multi=%zu params>0=%zu void_display=%zu\n",
      label, ir_present, frame_proven, zero_results, one_result, multi_results, params_visible,
      void_display);
    return std::tuple<std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t>(
      ir_present, frame_proven, zero_results, one_result, multi_results, void_display);
  };

  if ( const module_t *main126 = cached_main126() )
  {
    // (wasm-test/wasm-test/main.wasm, go1.26.5) — the recognizer coverage
    // as computed relations: the result classes partition the proven-frame
    // set, void dominates, and void-display is the zero-result bodies minus
    // the terminal-noreturn ones (empty ir.ret).
    const auto [ir, frame, zero, one, multi, void_display] = survey(*main126, "main.wasm");
    CHECK(ir > 0u);
    CHECK(frame > 0u);
    CHECK(frame == zero + one + multi);
    CHECK(zero > one); // void-returning functions dominate Go code
    CHECK(one > 0u);
    CHECK(multi > 0u);
    CHECK(void_display > 0u);
    CHECK(void_display <= zero); // display candidates ⊆ zero-result bodies
  }

  module_t balls;
  if ( parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                         &balls) )
  {
    const auto [ir, frame, zero, one, multi, void_display] = survey(balls, "gowasm-balls");
    CHECK(ir > 0u);
    CHECK(frame > 0u);
    CHECK(frame == zero + one + multi);
    CHECK(one > 0u);
    CHECK(multi > 0u); // interface/pair results are recognized too
    CHECK(void_display > 0u);
    CHECK(void_display <= zero);
  }
}

TEST_CASE("G9: Go method display names keep the pointer-receiver form (todo 3.5)")
{
  // The wasm mangling `pkg.__T_.m` is `pkg.(*T).m` in canonical Go display.
  CHECK(go_method_display_name("main.__Ball_.draw") == "main.(*Ball).draw");
  CHECK(go_method_display_name("main.__devNull_.Write") == "main.(*devNull).Write");
  CHECK(go_method_display_name("fmt.__stringer_.String") == "fmt.(*stringer).String");
  // Value receivers are already canonical (`Ball.draw`) and pass through.
  CHECK(go_method_display_name("main.devNull.Write") == "main.devNull.Write");
  CHECK(go_method_display_name("main.Ball.Update") == "main.Ball.Update");
  // Non-method and near-miss shapes stay untouched: plain functions,
  // single-dot names, empty type tails (`__` + `_`), type names without
  // the mangling tails, and package-less symbols.
  CHECK(go_method_display_name("runtime.morestack") == "runtime.morestack");
  CHECK(go_method_display_name("main") == "main");
  CHECK(go_method_display_name("main.___.draw") == "main.___.draw");
  CHECK(go_method_display_name("main.__Ball.draw") == "main.__Ball.draw");
  CHECK(go_method_display_name("__Ball_.draw") == "__Ball_.draw");
  // Degenerate inputs never transform.
  CHECK(go_method_display_name("") == "");
  CHECK(go_method_display_name(".") == ".");

  // The mangled form stays the module-level ground truth: the balls
  // pclntab/export names are NOT rewritten by the loader (only the IDA
  // presentation name is — entry.cpp).
  module_t balls;
  if ( !parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                          &balls) )
    return; // corpus absent on this machine
  bool saw_draw_mangled = false;
  const std::size_t imp = balls.imported_func_types.size();
  for ( std::size_t i = 0; i < balls.bodies.size() && !saw_draw_mangled; ++i )
  {
    const std::size_t fn_index = imp + i;
    for ( const export_t &entry : balls.function_names )
      if ( entry.index == fn_index && entry.name == "main.__Ball_.draw" )
      {
        saw_draw_mangled = true;
        break;
      }
    if ( !saw_draw_mangled && i < balls.pclntab_function_names.size()
         && balls.pclntab_function_names[i] == "main.__Ball_.draw" )
      saw_draw_mangled = true;
  }
  CHECK(saw_draw_mangled);
}
