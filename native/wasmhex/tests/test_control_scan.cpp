// Control-scan unit tests over real fixtures: br_table tables, try/catch EH
// entries, and the virtual-stub region shape of jtbl v2 (all before-br_table).
#include "doctest.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "../core/types.hpp"
#include "../core/binary/instruction_stream.hpp"
#include "../core/toolchains/emscripten/libcxx.hpp"
#include "../core/toolchains/go/eh.hpp"
#include "../core/toolchains/go/ir.hpp"

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

bool parse_fixture(module_t *module, const std::string &relative,
                   wasmhex_brtable_mode_t mode = wasmhex_brtable_mode_t::chain)
{
  const std::string bytes = read_file(std::string(WASMHEX_REPO_DIR) + relative);
  if ( bytes.empty() )
    return false;
  module->brtable_mode = mode;
  return module->parse(reinterpret_cast<const uchar *>(bytes.data()), bytes.size());
}

} // namespace

TEST_CASE("control scan: br_table arms and depths")
{
  module_t module;
  REQUIRE(parse_fixture(&module, "/tests/fixtures/br_table.wasm"));
  REQUIRE(module.bodies.size() == 1);
  const body_t &body = module.bodies[0];
  const body_t::control_t *table = nullptr;
  for ( const body_t::control_t &control : body.control )
    if ( control.opcode == 0x0E )
      table = &control;
  REQUIRE(table != nullptr);
  CHECK(table->targets.size() == 3); // inner, middle, default
  CHECK(table->depths.size() == 3);
  CHECK(table->depths[0] == 0);
  CHECK(table->depths[1] == 1);
  CHECK(table->depths[2] == 2);
  // All three targets must land inside the body's code range.
  for ( uint32 target : table->targets )
    CHECK(target < body.size);
}

TEST_CASE("control scan: large br_table (256 arms) stays flat")
{
  module_t module;
  REQUIRE(parse_fixture(&module, "/tests/fixtures/brtable/bt_depth_per_case_256.wasm"));
  size_t tables = 0, arms = 0;
  for ( const body_t::control_t &control : module.bodies[0].control )
    if ( control.opcode == 0x0E )
    {
      ++tables;
      arms += control.targets.size();
    }
  CHECK(tables >= 1);
  CHECK(arms >= 257);
}

TEST_CASE("control scan: S3 br_table carries no virtual stubs")
{
  module_t module;
  REQUIRE(parse_fixture(&module, "/tests/fixtures/br_table.wasm", wasmhex_brtable_mode_t::jtbl));
  const body_t &body = module.bodies[0];
  const body_t::control_t *table = nullptr;
  for ( const body_t::control_t &control : body.control )
    if ( control.opcode == 0x0E )
      table = &control;
  REQUIRE(table != nullptr);
  // The stub region is gone — metadata carries the targets directly
  // and the br_table keeps its original stream position.
  CHECK(table->stubs.empty());
  size_t stub_controls = 0;
  for ( const body_t::control_t &control : body.control )
    if ( control.opcode == 0xFF || control.opcode == 0xF2 )
      ++stub_controls;
  CHECK(stub_controls == 0);
}

TEST_CASE("control scan: EH tag sections are recorded")
{
  module_t module;
  REQUIRE(parse_fixture(&module, "/tests/fixtures/link_comdat_multi_tag_eh_winner.wasm"));
  // The committed EH fixtures are deliberately rejected by the core front
  // end, so the scan-level try/catch invariants are exercised through the
  // tag section here; the verdicts themselves are covered by the golden
  // oracle test.
  CHECK(module.tag_type_indices.size() >= 1);
}

TEST_CASE("Go provenance: dispatcher detection stays inside each function")
{
  module_t module;
  REQUIRE(parse_fixture(&module, "/tests/fixtures/g3_go_call_protocol.wasm"));
  REQUIRE(module.go_provenance);
  REQUIRE(module.go_function_shapes.size() == module.bodies.size());
  REQUIRE(module.bodies.size() == 5);
  // Three short functions precede the two Go-style callers. Previously a
  // scan up to the end of the code section borrowed the later br_table and
  // marked all three as dispatchers.
  for ( size_t i = 0; i < 3; ++i )
    CHECK((module.go_function_shapes[i] & 4) == 0);
  CHECK((module.go_function_shapes[3] & 4) != 0);
  CHECK((module.go_function_shapes[4] & 4) != 0);
}

TEST_CASE(
  "M3c/M3d: Go call scanner decodes multi-byte LEB memarg offsets and G4 caps caller-frame spills")
{
  module_t balls;
  if ( parse_fixture(&balls, "/wasm-test/gowasm-balls/main.wasm") )
  {
    // Multi-byte LEB memarg decode: any real Go corpus stores past offset
    // 127, so the 2-byte LEB (`37 03 88 01`-shaped) decode is exercised by
    // the census — no single body's offsets pinned.
    std::size_t multibyte_memarg = 0;
    for ( const body_t &body : balls.bodies )
      for ( const body_t::memory_access_t &access : body.memory_accesses )
        if ( access.memarg >= 128 )
          ++multibyte_memarg;
    CHECK(multibyte_memarg > 0);

    // Caller-frame spill exclusion (the outgoing-argument boundary filter):
    // a collected argument store always sits before its call site — a store
    // at/after the call is the caller-frame spill or a post-barrier store,
    // never an argument. This is the corpus-wide form of the runtime
    // .makeslice 3-argument ground truth.
    std::size_t calls = 0, args = 0;
    for ( const body_t &body : balls.bodies )
      for ( const body_t::go_call_t &gc : body.go_calls )
      {
        ++calls;
        for ( const auto &arg : gc.args )
        {
          CHECK(arg.store_offset < gc.call_offset);
          ++args;
        }
      }
    CHECK(calls > 0);
    CHECK(args > 0);
  }
}

TEST_CASE(
  "G6: structural terminal noreturn callee predicate, cold-tail reproducer, and corpus ground truth")
{
  // 1. Reproducer fixture: g6/g6_cold_tail_noreturn.wasm
  {
    module_t mod;
    REQUIRE(parse_fixture(&mod, "/tests/fixtures/g6/g6_cold_tail_noreturn.wasm"));
    CHECK(mod.is_go);
    CHECK(mod.go_provenance);
    REQUIRE(mod.bodies.size() == 4);

    // body[0] ($terminal_helper): zero 0x0F return, forward-only const-JMP -> qualifies.
    const go_function_ir_t &term_ir = mod.bodies[0].go_ir;
    CHECK(term_ir.present);
    CHECK(term_ir.unwind_exit != UINT32_MAX);
    CHECK(term_ir.pc_b_register_proof);
    CHECK(term_ir.ret.empty());
    REQUIRE(term_ir.jmps.size() == 1);
    CHECK(term_ir.resume_table.targets[term_ir.jmps[0].case_index] > term_ir.jmps[0].br_offset);
    CHECK(is_go_terminal_noreturn_function(term_ir, false));

    // Near-miss gates on `is_go_terminal_noreturn_function`:
    //   (a) morestack / morestack_noctxt must NOT qualify even if structurally identical;
    CHECK_FALSE(is_go_terminal_noreturn_function(term_ir, true));
    //   (b) non-empty `ret` (opcode 0x0F present) must NOT qualify;
    go_function_ir_t with_ret = term_ir;
    with_ret.ret.push_back(42u);
    CHECK_FALSE(is_go_terminal_noreturn_function(with_ret, false));
    //   (c) missing `present`, `unwind_exit`, or `pc_b_register_proof` must NOT qualify;
    go_function_ir_t no_present = term_ir;
    no_present.present = false;
    CHECK_FALSE(is_go_terminal_noreturn_function(no_present, false));
    go_function_ir_t no_unwind = term_ir;
    no_unwind.unwind_exit = UINT32_MAX;
    CHECK_FALSE(is_go_terminal_noreturn_function(no_unwind, false));
    go_function_ir_t no_pcb = term_ir;
    no_pcb.pc_b_register_proof = false;
    CHECK_FALSE(is_go_terminal_noreturn_function(no_pcb, false));
    //   (d) out-of-range `jmp.case_index` must NOT qualify;
    go_function_ir_t bad_case = term_ir;
    bad_case.jmps[0].case_index = uint32(bad_case.resume_table.targets.size());
    CHECK_FALSE(is_go_terminal_noreturn_function(bad_case, false));

    // body[1] ($loop_helper): near-miss with zero 0x0F return AND a backward jump
    // (`target_offset <= br_offset`) -> must NOT qualify.
    const go_function_ir_t &loop_ir = mod.bodies[1].go_ir;
    CHECK(loop_ir.present);
    CHECK(loop_ir.unwind_exit != UINT32_MAX);
    CHECK(loop_ir.pc_b_register_proof);
    CHECK(loop_ir.ret.empty());
    REQUIRE(loop_ir.jmps.size() == 1);
    CHECK(loop_ir.resume_table.targets[loop_ir.jmps[0].case_index] <= loop_ir.jmps[0].br_offset);
    CHECK_FALSE(is_go_terminal_noreturn_function(loop_ir, false));

    // body[2] (`cold_tail_severed`): 3 calls to `$terminal_helper` all marked `is_noreturn_call`.
    const go_function_ir_t &caller_ok = mod.bodies[2].go_ir;
    REQUIRE(caller_ok.calls.size() == 3);
    for ( const go_call_ir_t &c : caller_ok.calls )
    {
      CHECK(c.fn_index == 1u);
      CHECK(c.args.size() == 2u);
      CHECK(c.is_noreturn_call);
    }

    // Blob 'x' v3 round-trip preserves `is_noreturn_call` on all 3 call records.
    std::vector<uint32> encoded;
    encode_go_function_ir_blob(caller_ok, &encoded);
    go_function_ir_t decoded;
    REQUIRE(decode_go_function_ir_blob(encoded.data(), encoded.size(), &decoded));
    REQUIRE(decoded.calls.size() == 3);
    for ( size_t k = 0; k < 3; ++k )
      CHECK(decoded.calls[k].is_noreturn_call);

    // body[3] (`cold_tail_near_miss_loop`): 3 calls to `$loop_helper` NOT marked `is_noreturn_call`.
    const go_function_ir_t &caller_nm = mod.bodies[3].go_ir;
    REQUIRE(caller_nm.calls.size() == 3);
    for ( const go_call_ir_t &c : caller_nm.calls )
    {
      CHECK(c.fn_index == 2u);
      CHECK_FALSE(c.is_noreturn_call);
    }
  }

  // 2. Corpus census (main.wasm stripped + gowasm-balls named): every
  //    noreturn-marked call site points at a callee the predicate itself
  //    proves terminal, and every qualifying body really returns nothing —
  //    the corpus-wide form of the sigpanic/goPanicIndex vs
  //    panicwrap/bgsweep/morestack ground truth, no pinned body indices or
  //    call counts.
  auto noreturn_census = [](const module_t &mod, std::size_t *calls_out, std::size_t *qualify_out)
  {
    const std::size_t imp = mod.imported_func_types.size();
    *calls_out = 0;
    *qualify_out = 0;
    for ( const body_t &b : mod.bodies )
    {
      const go_function_ir_t &ir = b.go_ir;
      if ( ir.present && is_go_terminal_noreturn_function(ir, false) )
      {
        ++*qualify_out;
        CHECK(ir.ret.empty()); // a body with a recorded `return` never qualifies
      }
      for ( const go_call_ir_t &c : ir.calls )
        if ( c.is_noreturn_call )
        {
          ++*calls_out;
          // The mark is never guesswork: the callee's own IR must prove out.
          REQUIRE(c.fn_index != UINT32_MAX);
          REQUIRE(c.fn_index >= imp);
          REQUIRE(c.fn_index - imp < mod.bodies.size());
          CHECK(is_go_terminal_noreturn_function(mod.bodies[c.fn_index - imp].go_ir, false));
        }
    }
  };
  module_t main126;
  if ( parse_fixture(&main126, "/wasm-test/wasm-test/main.wasm") )
  {
    std::size_t noreturn_calls = 0, qualifying = 0;
    noreturn_census(main126, &noreturn_calls, &qualifying);
    CHECK(qualifying > 0);
    CHECK(noreturn_calls > 0);
  }
  module_t balls;
  if ( parse_fixture(&balls, "/wasm-test/gowasm-balls/main.wasm") )
  {
    std::size_t noreturn_calls = 0, qualifying = 0;
    noreturn_census(balls, &noreturn_calls, &qualifying);
    CHECK(qualifying > 0);
    CHECK(noreturn_calls > 0);
  }
}

TEST_CASE("M4/D4 GĐ2b: EH invoke marks cover the whole strict-name set (fragmented "
          "prologue, noreturn sites, identity-only)")
{
  // Unit layer: the three evidence classes of eh_invoke_site_ir_t validity.
  {
    using wasmhex::loader::eh_invoke_site_ir_t;
    eh_invoke_site_ir_t full{};
    full.call_offset = 40;
    full.threw_global = 285;
    full.tee_local = 1;
    full.zero_store_before = 20;
    full.read_offset = 50;
    full.zero_store_after = 56;
    CHECK(full.invoke_site_valid(100));

    // prologue-only (the noreturn-throw invoke path): no flag read after.
    eh_invoke_site_ir_t noreturn = full;
    noreturn.read_offset = UINT32_MAX;
    noreturn.zero_store_after = UINT32_MAX;
    noreturn.guard_br_offset = UINT32_MAX;
    noreturn.pad_offset = UINT32_MAX;
    CHECK(noreturn.invoke_site_valid(100));

    // identity-only (import-table identity; no protocol evidence claimed).
    eh_invoke_site_ir_t identity{};
    identity.call_offset = 40;
    CHECK(identity.invoke_site_valid(100));

    // garbage: evidence fields half-filled (not a recognized class).
    eh_invoke_site_ir_t half = identity;
    half.threw_global = 285;
    CHECK_FALSE(half.invoke_site_valid(100));

    // blob round-trip keeps the classes and stays fail-closed.
    std::vector<uint32> wire;
    wasmhex::loader::eh_function_ir_t ir;
    ir.present = true;
    ir.invokes.push_back(full);
    ir.invokes.push_back(noreturn);
    ir.invokes.push_back(identity);
    wasmhex::loader::encode_eh_function_ir_blob(ir, &wire);
    CHECK(wire.size() == 2u + 1u + 3u * 10u + 1u); // header+sites+padcount
    wasmhex::loader::eh_function_ir_t back;
    REQUIRE(wasmhex::loader::decode_eh_function_ir_blob(wire.data(), wire.size(), &back));
    REQUIRE(back.invokes.size() == 3);
    CHECK(back.invokes[0].read_offset == 50u);
    CHECK(back.invokes[1].read_offset == UINT32_MAX);
    CHECK(back.invokes[2].zero_store_before == UINT32_MAX);
    CHECK_FALSE(wasmhex::loader::decode_eh_function_ir_blob(wire.data(), wire.size() - 1, &back));
  }

  module_t duckdb;
  if ( parse_fixture(&duckdb, "/wasm-test/wasm-test/duckdb.wasm") )
  {
    const std::size_t imports = duckdb.imported_func_types.size();
    std::size_t name_sites = 0, unmarked = 0, marks = 0, protocol_full = 0, noreturn = 0,
                identity_only = 0, pads = 0;
    std::map<std::pair<std::size_t, uint32>, bool> marked;
    for ( std::size_t bi = 0; bi < duckdb.bodies.size(); ++bi )
    {
      const body_t &body = duckdb.bodies[bi];
      for ( const wasmhex::loader::eh_invoke_site_ir_t &s : body.eh_ir.invokes )
      {
        ++marks;
        if ( s.read_offset != UINT32_MAX )
          ++protocol_full;
        else if ( s.zero_store_before != UINT32_MAX )
          ++noreturn;
        else
          ++identity_only;
        marked[{ bi, s.call_offset }] = true;
      }
      pads += body.eh_ir.pads.size();
      if ( body.offset >= duckdb.code.size() || body.size == 0 )
        continue;
      const std::size_t avail
        = std::min<std::size_t>(body.size, duckdb.code.size() - std::size_t(body.offset));
      binary::insn_cursor_t cursor{ duckdb.code.data() + body.offset, avail, 0, nullptr };
      binary::decoded_insn_t d;
      while ( cursor.pos < avail && binary::next_insn(cursor, &d) )
      {
        if ( d.opcode != 0x10 || d.imm[0] >= imports )
          continue;
        if ( !wasmhex::loader::is_invoke_helper_name(
               duckdb.function_imports[d.imm[0]].name.c_str()) )
          continue;
        ++name_sites;
        if ( marked.count({ bi, d.offset }) == 0 )
          ++unmarked;
      }
    }
    CHECK(name_sites > 0u);
    CHECK(unmarked == 0u);      // The name-fallback set is fully covered
    CHECK(marks >= name_sites); // every invoke-named site carries a mark
    CHECK(protocol_full > 0u);  // all three evidence classes occur
    CHECK(noreturn > 0u);
    CHECK(identity_only > 0u);
    CHECK(pads > 0u);
  }
}

TEST_CASE("M4/D4 GĐ3: libc++ SSO dtor marks proven at the IR (blob 'l')")
{
  // Codec layer: round-trip + truncation fail-closed + form bound.
  {
    using wasmhex::loader::libcxx_function_ir_t;
    using wasmhex::loader::libcxx_sso_dtor_ir_t;
    libcxx_sso_dtor_ir_t site;
    site.guard_offset = 40;
    site.tag_load_offset = 20;
    site.free_call_offset = 50;
    site.free_fn = 47599;
    site.form = 1;
    CHECK(site.valid(100));
    CHECK_FALSE(site.valid(40)); // guard at the end of the body
    libcxx_function_ir_t ir;
    ir.present = true;
    ir.sso_dtors.push_back(site);
    std::vector<uint32> wire;
    wasmhex::loader::encode_libcxx_function_ir_blob(ir, &wire);
    CHECK(wire.size() == 2u + 1u + 5u);
    libcxx_function_ir_t back;
    REQUIRE(wasmhex::loader::decode_libcxx_function_ir_blob(wire.data(), wire.size(), &back));
    REQUIRE(back.sso_dtors.size() == 1);
    CHECK(back.sso_dtors[0].guard_offset == 40u);
    CHECK(back.sso_dtors[0].free_fn == 47599u);
    CHECK_FALSE(
      wasmhex::loader::decode_libcxx_function_ir_blob(wire.data(), wire.size() - 1, &back));
    wire.back() = 9; // invalid form byte
    CHECK_FALSE(wasmhex::loader::decode_libcxx_function_ir_blob(wire.data(), wire.size(), &back));
  }

  // Corpus layer: duckdb.wasm carries exactly the pointer-tag8 form with
  // the string at offset 0 of its holder (tag +11 / data +0, the shape the
  // ctree matcher recognized by name); member strings (tag at +27 etc.)
  // fail the offsets in BOTH layers and stay untouched.
  module_t duckdb;
  if ( parse_fixture(&duckdb, "/wasm-test/wasm-test/duckdb.wasm") )
  {
    std::size_t bodies = 0, sites = 0, form1 = 0;
    for ( const body_t &body : duckdb.bodies )
      if ( body.libcxx_ir.present )
      {
        ++bodies;
        for ( const wasmhex::loader::libcxx_sso_dtor_ir_t &s : body.libcxx_ir.sso_dtors )
        {
          ++sites;
          if ( s.form == 1 )
            ++form1;
          CHECK(s.free_fn != UINT32_MAX);
          CHECK(s.tag_load_offset < s.guard_offset);
          CHECK(s.guard_offset < s.free_call_offset);
        }
      }
    CHECK(bodies > 0u);
    CHECK(sites > 0u);
    // Every corpus site is the pointer-tag8 form (tag +11 / data +0) — the
    // computed relation form1 == sites, not a pair of pinned counts.
    CHECK(form1 == sites);
  }
}
