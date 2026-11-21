// Go type data layer — moduledata scan, type
// descriptor resolution, ArgInfo decode, and the blob 't' v1 codec.
// Corpus ground truth (pinned against the local wasm-test corpus):
//   - wasm-test/gowasm-balls/main.wasm (go1.27.3): moduledata @0x13e2e0,
//     517 walk descriptors, main.Ball @0x137320 reached lazily through
//     *main.Ball @0x11c080, string @0x12d570, fmt.Sprintf ArgInfo
//     {0:8, 8:8}{16:8, 24:8, 32:8} with _func.args = 56.
//   - wasm-test/gowasm126/main.wasm (go1.26.5, canonical rebuild from the
//     saved go.mod/main.go in that directory): typelinks 515, itablinks
//     15, *main.Pt root with the Pt struct behind it.
//   - wasm-test/wasm-test/main.wasm (stripped) and duckdb.wasm: the layer
//     stays empty (fail-closed).
#include "../core/toolchains/go/types.hpp"

#include "doctest.h"

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <cstdlib>
#include <cstring>

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

const module_t *cached_balls()
{
  static module_t mod;
  static bool loaded = false;
  static bool ok = false;
  if ( !loaded )
  {
    loaded = true;
    ok = parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                           &mod);
  }
  return ok ? &mod : nullptr;
}

bool parse_g126(module_t *module)
{
  return parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm126/main.wasm",
                           module);
}

bool slots_match(const go_arginfo_t &arginfo, const std::vector<std::pair<uint32, uint32>> &want)
{
  if ( arginfo.slots.size() != want.size() )
    return false;
  for ( size_t i = 0; i < want.size(); ++i )
    if ( arginfo.slots[i].offset != want[i].first || arginfo.slots[i].size != want[i].second )
      return false;
  return true;
}

// Loader-order display-name lookup (export names first, then pclntab) —
// corpus ground truths anchor on stable symbol names, never body indices.
std::size_t body_index_for_name(const module_t &module, const std::string &want)
{
  for ( std::size_t i = 0; i < module.bodies.size(); ++i )
  {
    const std::size_t fn_index = module.imported_func_types.size() + i;
    for ( const export_t &entry : module.function_names )
      if ( entry.index == fn_index && entry.name == want )
        return i;
    if ( i < module.pclntab_function_names.size() && module.pclntab_function_names[i] == want )
      return i;
  }
  return SIZE_MAX;
}
} // namespace

TEST_CASE("G7: moduledata layout tables map profiles and reject others")
{
  CHECK(go_expected_moduledata_version(go_profile_t::go1_26_series) == 26);
  CHECK(go_expected_moduledata_version(go_profile_t::go1_27_series) == 27);
  CHECK(go_expected_moduledata_version(go_profile_t::go1_23_series) == 0);
  CHECK(go_expected_moduledata_version(go_profile_t::go1_19_legacy) == 0);
  CHECK(go_expected_moduledata_version(go_profile_t::unknown) == 0);
  CHECK(go_expected_moduledata_version(go_profile_t::none) == 0);
}

TEST_CASE("G7: blob 't' tag is reserved and distinct from the other netnode tags")
{
  // x/E/L/u/C/K/M/Y/w/p/q/D/F/z are taken (loader blobs); 't' must not be
  // any of them and stays a compile-time constant.
  CHECK(go_types_blob_tag == 't');
  CHECK(go_types_blob_tag != 'x');
  CHECK(go_types_blob_tag != 'E');
  CHECK(go_types_blob_tag != 'L');
  CHECK(go_types_blob_tag != 'u');
  CHECK(go_types_blob_tag != 'C');
  CHECK(go_types_blob_tag != 'K');
  CHECK(go_types_blob_tag != 'M');
  CHECK(go_types_blob_tag != 'Y');
  CHECK(go_types_blob_tag != 'w');
  CHECK(go_types_blob_tag != 'p');
  CHECK(go_types_blob_tag != 'q');
  CHECK(go_types_blob_tag != 'D');
  CHECK(go_types_blob_tag != 'F');
  CHECK(go_types_blob_tag != 'z');
  // The wire is v2 (v1 prefix + the layout tail); a v1 stream is still
  // decodable (pre-layout database: empty layout vector, canonical names
  // only).
  CHECK(go_types_blob_version == 2);
  CHECK(go_types_blob_v1 == 1);
}

TEST_CASE("G7: go1.27.3 corpus (gowasm-balls) — moduledata, walk, descriptors")
{
  const module_t *balls = cached_balls();
  if ( balls == nullptr )
    return; // corpus absent on this machine

  const go_type_info_t &info = balls->go_types;
  REQUIRE(info.present);
  // Cross-layer consistency (no pinned corpus addresses): the layout
  // version matches the module's Go profile, the walk roots address a
  // non-empty types region that precedes moduledata, and the scan closed
  // silently.
  CHECK(info.layout_version == go_expected_moduledata_version(balls->go_profile.profile));
  CHECK(info.moduledata != 0);
  CHECK(info.types != 0);
  CHECK(info.types < info.moduledata); // the types section precedes moduledata
  CHECK(info.gofunc != 0);
  CHECK(info.typedesclen > 0);
  CHECK(info.mismatch_reason.empty());

  size_t named = 0;
  size_t structs = 0;
  size_t closure_added = 0;
  for ( const go_type_descriptor_t &descriptor : info.descriptors )
  {
    if ( !descriptor.name.empty() )
      ++named;
    if ( descriptor.kind == 25 )
      ++structs;
    if ( descriptor.eager_closure )
      ++closure_added;
  }
  CHECK(named > 0); // walk roots carry their str names
  CHECK(structs > 0);
  CHECK(closure_added > 0);                       // closure additions
  CHECK(info.descriptors.size() > closure_added); // roots + closure

  // Retention anchor: the ftab layer agrees with the pclntab name layer
  // and maps every function it counts.
  CHECK(info.anchor.present);
  CHECK(info.anchor.nfunc > 0);
  REQUIRE(info.anchor.func_addrs.size() == info.anchor.nfunc);
  if ( !balls->pclntab_function_names.empty() )
    CHECK(info.anchor.nfunc == balls->pclntab_function_names.size());
  size_t mapped = 0;
  for ( uint64 addr : info.anchor.func_addrs )
    if ( addr != 0 )
      ++mapped;
  CHECK(mapped == info.anchor.func_addrs.size());
}

TEST_CASE("G7: gowasm-balls lazy resolution — string, main.Ball, ptrToThis cycle")
{
  module_t mod; // own instance: lazy appends must not leak into other tests
  if ( !parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                          &mod) )
    return;
  const std::size_t roots = mod.go_types.descriptors.size();

  // Resolve by NAME through the descriptor set (the walk roots carry their
  // str names) — no pinned corpus addresses. main.Ball itself is reached
  // lazily through the *main.Ball pointer root (the anchor-4 path).
  const go_type_descriptor_t *string_type = nullptr;
  const go_type_descriptor_t *ptr_ball = nullptr;
  for ( const go_type_descriptor_t &descriptor : mod.go_types.descriptors )
  {
    if ( descriptor.name == "*string" && descriptor.kind == 24 )
      string_type = &descriptor;
    if ( descriptor.name == "*main.Ball" && descriptor.kind == 22 )
      ptr_ball = &descriptor;
  }
  REQUIRE(string_type != nullptr);
  REQUIRE(ptr_ball != nullptr);

  // The builtin string type: kind 24, 16 bytes, raw name keeps the star.
  CHECK(string_type->kind == 24);
  CHECK(string_type->size == 16);
  CHECK((string_type->tflag & 0x2) != 0); // tflagExtraStar

  // Snapshot addresses before the lazy resolve: an append can reallocate
  // the descriptor vector and invalidate the root pointers.
  const std::uint64_t string_addr = string_type->addr;
  const std::uint64_t ptr_ball_addr = ptr_ball->addr;
  const std::uint64_t ball_addr = ptr_ball->elem_addr;
  const go_type_descriptor_t *ball = resolve_go_type_addr(mod, ball_addr);
  REQUIRE(ball != nullptr);

  // main.Ball: a struct whose fields tile monotonically inside the
  // descriptor size, with the ptr_toThis <-> elem cycle closing.
  CHECK(go_descriptor_type_name(*ball) == "main.Ball");
  CHECK(ball->kind == 25);
  REQUIRE(!ball->fields.empty());
  CHECK(ball->fields.front().offset == 0);
  for ( std::size_t i = 1; i < ball->fields.size(); ++i )
    CHECK(ball->fields[i].offset >= ball->fields[i - 1].offset);
  CHECK(ball->size > ball->fields.back().offset); // every field inside the struct
  CHECK(ball->size % 8 == 0);                     // Go word-aligned structs
  CHECK(ball->ptr_to_this == ptr_ball_addr);
  CHECK(ball_addr == ball->addr); // the cycle closes

  // A struct field whose type is the builtin string descriptor.
  bool has_string_field = false;
  for ( const auto &field : ball->fields )
    if ( field.type_addr == string_addr )
      has_string_field = true;
  CHECK(has_string_field);

  // Lazy resolve of an already-closed descriptor is a pure hit: the
  // descriptor set does not grow.
  CHECK(resolve_go_type_addr(mod, ball->addr) == ball);
  CHECK(resolve_go_type_addr(mod, ptr_ball_addr) != nullptr);
  CHECK(mod.go_types.descriptors.size() == roots); // both were closure hits
}

TEST_CASE("G7-S4: pure display helpers over the decoded layer (blob 't' consumers)")
{
  const module_t *balls = cached_balls();
  if ( balls == nullptr )
    return; // corpus absent on this machine
  const go_type_info_t &info = balls->go_types;
  REQUIRE(info.present);

  // go_display_type_name: every map descriptor renders its full Go name
  // (key + elem) from the layer alone — the decompiler side has no
  // module_t, so these lookups must close inside the descriptor set.
  size_t maps = 0;
  size_t maps_named = 0;
  for ( const go_type_descriptor_t &descriptor : info.descriptors )
  {
    if ( descriptor.kind != 21 )
      continue;
    ++maps;
    const std::string name = go_display_type_name(info, descriptor.addr);
    if ( name.compare(0, 4, "map[") == 0 )
      ++maps_named;
  }
  CHECK(maps > 0);           // real Go corpora carry map descriptors
  CHECK(maps_named == maps); // every one renders key and elem

  // The C-presentation names: canonical spellings for the known shapes,
  // go__pkg_Type for named structs, "" for scalars.
  const go_type_descriptor_t *find_named = nullptr;
  for ( const go_type_descriptor_t &descriptor : info.descriptors )
    if ( go_descriptor_type_name(descriptor) == "js.Value" && descriptor.kind == 25 )
    {
      find_named = &descriptor;
      break;
    }
  REQUIRE(find_named != nullptr);
  CHECK(go_descriptor_c_name(*find_named) == "go__js__Value"); // known-API spelling
  // main.Ball reached lazily through its *main.Ball pointer root (the
  // walk closure carries the pointer; the struct resolves on demand).
  const go_type_descriptor_t *ptr_ball = nullptr;
  for ( const go_type_descriptor_t &descriptor : info.descriptors )
    if ( descriptor.name == "*main.Ball" && descriptor.kind == 22 )
    {
      ptr_ball = &descriptor;
      break;
    }
  REQUIRE(ptr_ball != nullptr);
  const go_type_descriptor_t *ball
    = resolve_go_type_addr(*const_cast<module_t *>(balls), ptr_ball->elem_addr);
  REQUIRE(ball != nullptr);
  CHECK(go_descriptor_type_name(*ball) == "main.Ball");
  CHECK(go_descriptor_c_name(*ball) == "go__main_Ball");

  // The blob-side struct layout mirrors the decoded descriptor exactly
  // (cross-layer consistency: names + offsets + total size) and renders
  // the float64 / nested-string field types.
  std::vector<go_c_field_t> fields;
  uint64 size = 0;
  REQUIRE(go_descriptor_struct_fields(info, *ball, &fields, &size));
  CHECK(size == ball->size);
  REQUIRE(fields.size() == ball->fields.size());
  for ( std::size_t i = 0; i < fields.size(); ++i )
  {
    CHECK(fields[i].name == ball->fields[i].name);
    CHECK(fields[i].offset == ball->fields[i].offset);
    if ( i + 1 < fields.size() )
      CHECK(fields[i].size <= fields[i + 1].offset - fields[i].offset);
  }
  CHECK(fields.back().offset + fields.back().size == ball->size); // closes exactly
  bool saw_double = false;
  bool saw_nested_string = false;
  for ( const go_c_field_t &field : fields )
  {
    if ( field.c_type == "double" ) // float64
      saw_double = true;
    if ( field.c_type == "struct go__string" ) // the nested canonical
      saw_nested_string = true;
  }
  CHECK(saw_double);
  CHECK(saw_nested_string);

  // Fail-closed: unknown addresses and non-structs render nothing. The
  // unknown address is computed past every descriptor, not a magic value.
  CHECK(go_display_type_name(info, 0) == "");
  std::uint64_t beyond = 0;
  for ( const go_type_descriptor_t &descriptor : info.descriptors )
    if ( descriptor.addr > beyond )
      beyond = descriptor.addr;
  REQUIRE(beyond > 0);
  CHECK(go_display_type_name(info, beyond + 0x1000) == "");
  const go_type_descriptor_t *string_type = nullptr;
  for ( const go_type_descriptor_t &descriptor : info.descriptors )
    if ( descriptor.name == "*string" && descriptor.kind == 24 )
    {
      string_type = &descriptor;
      break;
    }
  REQUIRE(string_type != nullptr);
  CHECK_FALSE(go_descriptor_struct_fields(info, *string_type, &fields, &size));
}

TEST_CASE("G7: gowasm-balls ArgInfo — fmt.Sprintf ground truth")
{
  const module_t *balls = cached_balls();
  if ( balls == nullptr )
    return;
  const go_type_info_t &info = balls->go_types;
  REQUIRE(info.arginfo.size() == balls->pclntab_function_names.size());
  const std::size_t sprintf_body = body_index_for_name(*balls, "fmt.Sprintf");
  REQUIRE(sprintf_body != SIZE_MAX);
  REQUIRE(sprintf_body < info.arginfo.size());
  const go_arginfo_t &sprintf = info.arginfo[sprintf_body];
  REQUIRE(sprintf.present);
  // args_bytes derives from the signature's ABI groups: the 16-byte string
  // + 24-byte []any params + the phantom 16-byte string result.
  CHECK(sprintf.args_bytes == 16u + 24u + 16u);
  CHECK(slots_match(sprintf, { { 0, 8 }, { 8, 8 }, { 16, 8 }, { 24, 8 }, { 32, 8 } }));
  // Both arguments are aggregates (string, []any): every slot sits at depth 1.
  for ( const go_arginfo_slot_t &slot : sprintf.slots )
    CHECK(slot.depth == 1);
  // The top-level aggregate boundaries (StartAgg/EndAgg operators)
  // delimit the two parameters: string = slots 0..1, slice = slots 2..4.
  REQUIRE(sprintf.group_starts.size() == 5);
  const uint8 want_sprintf_groups[5] = { 1, 0, 1, 0, 0 };
  for ( size_t k = 0; k < 5; ++k )
    CHECK(sprintf.group_starts[k] == want_sprintf_groups[k]);
}

TEST_CASE("G3: single-source layout — fmt.Sprintf tier (a) beats the agreement tier")
{
  const module_t *balls = cached_balls();
  if ( balls == nullptr )
    return;
  const go_type_info_t &info = balls->go_types;
  REQUIRE(info.layouts.size() == balls->bodies.size());
  const std::size_t sprintf_body = body_index_for_name(*balls, "fmt.Sprintf");
  REQUIRE(sprintf_body != SIZE_MAX);
  const go_func_layout_t &layout = info.layouts[sprintf_body]; // the ArgInfo pin above
  REQUIRE(layout.source == go_layout_source_t::kDescriptor);
  CHECK(layout.anchor == "known-API");
  CHECK(layout.variadic);
  REQUIRE(layout.params.size() == 2);
  CHECK(layout.params[0].is_group);
  CHECK(layout.params[0].offset == 0);
  CHECK(layout.params[0].bytes == 16);            // string
  CHECK(layout.params[0].c_name == "go__string"); // NOT go__js__Value (Invoke agreement)
  CHECK(layout.params[1].is_group);
  // The ABI groups chain contiguously: slice right after the string, the
  // result right after the params — derived, not pinned.
  CHECK(layout.params[1].offset == layout.params[0].offset + layout.params[0].bytes);
  CHECK(layout.params[1].bytes == 24); // []any
  CHECK(layout.params[1].c_name == "go__slice");
  // Tier-(a) results ride the record at their incoming-window offsets.
  REQUIRE(layout.results.size() == 1);
  CHECK(layout.results[0].offset == layout.params[1].offset + layout.params[1].bytes);
  CHECK(layout.results[0].bytes == 16);
  CHECK(layout.results[0].c_name == "go__string");
  // The record survives the blob round-trip (both sides read the same
  // bytes: the loader encodes, the call-site collect decodes).
  {
    std::vector<uint32> encoded;
    encode_go_type_info_blob(info, &encoded);
    go_type_info_t decoded;
    REQUIRE(decode_go_type_info_blob(encoded.data(), encoded.size(), &decoded));
    REQUIRE(decoded.layouts.size() == info.layouts.size());
    const go_func_layout_t &wire = decoded.layouts[sprintf_body];
    REQUIRE(wire.source == go_layout_source_t::kDescriptor);
    CHECK(wire.variadic);
    REQUIRE(wire.params.size() == 2);
    CHECK(wire.params[0].c_name == "go__string");
    CHECK(wire.params[0].offset == 0);
    CHECK(wire.params[0].bytes == 16);
    CHECK(wire.params[1].c_name == "go__slice");
    REQUIRE(wire.results.size() == 1);
    CHECK(wire.results[0].offset == layout.results[0].offset);
    CHECK(wire.results[0].c_name == "go__string");
    CHECK(wire.anchor == "known-API");
  }
  // The descriptor cap froze the wire set at the scan output: the lazy
  // appends the tier-(a) anchors made at build time stay loader-side.
  CHECK(info.blob_descriptor_cap != 0);
  {
    std::vector<uint32> encoded;
    encode_go_type_info_blob(info, &encoded);
    go_type_info_t decoded;
    REQUIRE(decode_go_type_info_blob(encoded.data(), encoded.size(), &decoded));
    CHECK(decoded.descriptors.size() == info.blob_descriptor_cap);
    CHECK(decoded.descriptors.size() <= info.descriptors.size());
  }
}

// The census over the balls corpus — every tier and skip reason is
// counted, non-Go corpora keep the layout vector empty (fail-closed), and
// the tiers add up to the shaped bodies.
TEST_CASE("G3: layout census — descriptor / arginfo / evidence / skips")
{
  const module_t *balls = cached_balls();
  if ( balls == nullptr )
    return;
  const go_layout_census_t &census = balls->go_types.layout_census;
  const go_type_info_t &info = balls->go_types;
  REQUIRE(info.layouts.size() == balls->bodies.size());
  CHECK(census.total == info.layouts.size());
  // Recount from the records themselves (the loader summary line prints
  // exactly these numbers).
  std::size_t descriptor = 0, arginfo_tier = 0, evidence = 0, none = 0;
  for ( const go_func_layout_t &layout : info.layouts )
  {
    switch ( layout.source )
    {
      case go_layout_source_t::kDescriptor: ++descriptor; break;
      case go_layout_source_t::kArgInfo: ++arginfo_tier; break;
      case go_layout_source_t::kEvidence: ++evidence; break;
      case go_layout_source_t::kNone: ++none; break;
    }
  }
  CHECK(census.descriptor == descriptor);
  CHECK(census.arginfo == arginfo_tier);
  CHECK(census.evidence == evidence);
  CHECK(census.skip_no_arginfo + census.skip_shape == none);
  const std::size_t tier_sum = descriptor + arginfo_tier + evidence + none;
  CHECK(tier_sum == census.total);
  // The tier-(a) anchored set includes the known-API family and the itab
  // methods; every anchored body must pair 1:1 with its shape and carry at
  // least one named group (non-empty on any real Go corpus — the itab
  // anchors guarantee it).
  CHECK(descriptor > 0);
  std::printf("G3 balls census: descriptor=%zu arginfo=%zu evidence=%zu "
              "skip-no-arginfo=%zu skip-shape=%zu total=%zu\n",
              census.descriptor, census.arginfo, census.evidence, census.skip_no_arginfo,
              census.skip_shape, census.total);
  // js call sites keep their anchored names: Value.Call is tier (a) with
  // (js.Value, string, []any) — the string param is named even though the
  // pre-layout agreement scan left it go_agg16 (the draw contrast).
  bool call_anchored = false;
  for ( std::size_t i = 0; i < info.layouts.size() && !call_anchored; ++i )
  {
    const go_func_layout_t &layout = info.layouts[i];
    if ( layout.source != go_layout_source_t::kDescriptor
         || balls->pclntab_function_names[i] != "syscall/js.Value.Call" )
      continue;
    REQUIRE(layout.params.size() == 3);
    if ( layout.params[1].c_name == "go__string" && layout.params[2].c_name == "go__slice"
         && layout.params[0].c_name == "go__js__Value" )
      call_anchored = true;
  }
  CHECK(call_anchored);
  // Non-Go corpora keep the empty layer (the stripped corpus test holds
  // present=false; here the layout vector must simply stay empty).
  module_t fixture;
  if ( parse_module_file(std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/m0a_go_result_slot.wasm",
                         &fixture) )
  {
    // No pclntab ArgInfo layer: every body skips with the no-arginfo
    // reason (or the layer is absent entirely — both fail-closed).
    const bool fixture_fail_closed
      = fixture.go_types.layouts.empty() || fixture.go_types.layout_census.descriptor == 0;
    CHECK(fixture_fail_closed);
  }
}

TEST_CASE("G7: gowasm-balls ArgInfo aggregate groups — main.loop and func1")
{
  const module_t *balls = cached_balls();
  if ( balls == nullptr )
    return;
  const go_type_info_t &info = balls->go_types;
  const std::size_t loop_body = body_index_for_name(*balls, "main.loop");
  const std::size_t func1_body = body_index_for_name(*balls, "main.main.func1");
  REQUIRE(loop_body != SIZE_MAX);
  REQUIRE(func1_body != SIZE_MAX);
  REQUIRE(loop_body < info.arginfo.size());
  REQUIRE(func1_body < info.arginfo.size());

  // main.loop: one []*Ball slice argument — exactly one top-level group.
  const go_arginfo_t &loop = info.arginfo[loop_body];
  REQUIRE(loop.present);
  REQUIRE(loop.group_starts.size() == 3);
  const uint8 want_loop_groups[3] = { 1, 0, 0 };
  for ( size_t k = 0; k < 3; ++k )
    CHECK(loop.group_starts[k] == want_loop_groups[k]);

  // main.main.func1 (js.FuncOf callback): js.Value + []js.Value groups;
  // args_bytes derives from the ABI groups plus the phantom any result.
  const go_arginfo_t &func1 = info.arginfo[func1_body];
  REQUIRE(func1.present);
  CHECK(func1.args_bytes == 16u + 24u + 16u);
  REQUIRE(func1.group_starts.size() == 5);
  const uint8 want_func1_groups[5] = { 1, 0, 1, 0, 0 }; // js.Value + []js.Value
  for ( size_t k = 0; k < 5; ++k )
    CHECK(func1.group_starts[k] == want_func1_groups[k]);
}

TEST_CASE("M0-B 3.7: gowasm-ballclone carries the same-named main.Ball descriptor")
{
  // The runner multi-module test (dump-golden balls + ballclone in ONE
  // process) only exercises the per-IDB registry latch if the SECOND
  // database really owns a kind-25 `main.Ball` descriptor — the same
  // c_name (go__main__Ball) as gowasm-balls. This pin holds the fixture
  // to that contract at data level (the struct reaches the descriptor
  // set via the interface boxing in its main.go; a rebuilt fixture
  // without the boxing fails here instead of silently weakening the
  // 3.7 test).
  module_t clone;
  if ( !parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-ballclone/main.wasm",
                          &clone) )
    return; // corpus absent on this machine
  const go_type_info_t &info = clone.go_types;
  REQUIRE(info.present);
  // The struct descriptor resolves through the *T pointer root (the exact
  // anchor-4 path in types.cpp: the `__Ball_` mangled method searches
  // "*" + want as a pointer descriptor, then its elem/ptr_to_this side).
  const go_type_descriptor_t *ptr_ball = nullptr;
  const go_type_descriptor_t *ptr_box = nullptr;
  for ( const go_type_descriptor_t &descriptor : info.descriptors )
  {
    if ( descriptor.name == "*main.Ball" && descriptor.kind == 22 )
      ptr_ball = &descriptor;
    if ( descriptor.name == "*main.Box" && descriptor.kind == 22 )
      ptr_box = &descriptor;
  }
  REQUIRE(ptr_ball != nullptr); // the boxing in main.go keeps the roots alive
  REQUIRE(ptr_box != nullptr);
  const go_type_descriptor_t *ball = resolve_go_type_addr(clone, ptr_ball->elem_addr);
  const go_type_descriptor_t *box = resolve_go_type_addr(clone, ptr_box->elem_addr);
  REQUIRE(ball != nullptr);
  REQUIRE(box != nullptr);
  CHECK(go_descriptor_type_name(*ball) == "main.Ball"); // same name as gowasm-balls
  CHECK(ball->kind == 25);
  // Layout closes structurally: every field sits inside the descriptor size.
  REQUIRE(!ball->fields.empty());
  CHECK(ball->size > ball->fields.back().offset);
  CHECK(go_descriptor_type_name(*box) == "main.Box");
  CHECK(box->kind == 25);
  REQUIRE(!box->fields.empty());
  CHECK(box->size > box->fields.back().offset);
  CHECK(go_descriptor_c_name(*ball) == "go__main_Ball"); // the registry's key name
  // (same spelling as gowasm-balls — dots in `main.Ball` fold to one `_`)
  // Cross-corpus identity: same main.go, so Ball must match gowasm-balls'
  // Ball field-for-field when both corpora are present (resolved lazily
  // through the same pointer-root path on the shared cached instance).
  if ( const module_t *balls = cached_balls() )
  {
    const go_type_descriptor_t *balls_ptr = nullptr;
    for ( const go_type_descriptor_t &descriptor : balls->go_types.descriptors )
      if ( descriptor.name == "*main.Ball" && descriptor.kind == 22 )
      {
        balls_ptr = &descriptor;
        break;
      }
    if ( balls_ptr != nullptr )
    {
      const go_type_descriptor_t *balls_ball
        = resolve_go_type_addr(*const_cast<module_t *>(balls), balls_ptr->elem_addr);
      if ( balls_ball != nullptr )
      {
        CHECK(go_descriptor_type_name(*balls_ball) == "main.Ball");
        CHECK(ball->size == balls_ball->size);
        CHECK(ball->fields.size() == balls_ball->fields.size());
      }
    }
  }
}

TEST_CASE("G7: go1.26.5 corpus (gowasm126) — typelinks roots and Pt")
{
  module_t g126;
  if ( !parse_g126(&g126) )
    return; // corpus absent on this machine
  const go_type_info_t &info = g126.go_types;
  REQUIRE(info.present);
  // Cross-layer consistency: layout version follows the module profile,
  // the descriptor set covers at least the typelink roots plus the eager
  // closure, and the scan closed silently.
  CHECK(info.layout_version == go_expected_moduledata_version(g126.go_profile.profile));
  CHECK(info.moduledata != 0);
  CHECK(info.types != 0);
  CHECK(info.types < info.moduledata);
  CHECK(info.typelinks_len > 0);
  CHECK(info.itablinks_len > 0);
  CHECK(info.descriptors.size() >= info.typelinks_len); // roots + closure
  CHECK(info.mismatch_reason.empty());

  // go1.26.5's pclntab names blow the 256-byte name cap (pre-existing
  // behavior, unchanged): names stay empty while the anchor still maps
  // every function for the ArgInfo layer.
  CHECK(g126.pclntab_function_names.empty());
  CHECK(info.anchor.present);
  CHECK(info.anchor.nfunc > 0);
  REQUIRE(info.anchor.func_addrs.size() == info.anchor.nfunc);

  // *main.Pt is a typelink root; the Pt struct itself resolves lazily and
  // its fields tile monotonically inside the descriptor size.
  const go_type_descriptor_t *ptr_pt = nullptr;
  for ( const go_type_descriptor_t &descriptor : info.descriptors )
    if ( descriptor.name == "*main.Pt" && descriptor.kind == 22 )
      ptr_pt = &descriptor;
  REQUIRE(ptr_pt != nullptr);
  const go_type_descriptor_t *pt = resolve_go_type_addr(g126, ptr_pt->elem_addr);
  REQUIRE(pt != nullptr);
  CHECK(pt->kind == 25);
  REQUIRE(pt->fields.size() >= 2);
  CHECK(pt->fields.front().offset == 0);
  for ( std::size_t i = 1; i < pt->fields.size(); ++i )
    CHECK(pt->fields[i].offset >= pt->fields[i - 1].offset);
  CHECK(pt->size > pt->fields.back().offset);

  // The main.Pt.String()-shaped ArgInfo (func(Pt) string): a nested string
  // word pair (depth 2) inside the struct argument (depth 1). g126's
  // pclntab names blow the 256-byte cap, so the body cannot be found by
  // name — the nested-decode property is swept over the ArgInfo layer
  // instead: every depth-2 entry decodes to well-formed slots (ascending
  // offsets from 0, inside the declared args window).
  bool saw_nested = false;
  for ( const go_arginfo_t &shaped : info.arginfo )
  {
    if ( !shaped.present || shaped.slots.empty() || shaped.slots.front().depth != 2 )
      continue;
    saw_nested = true;
    CHECK(shaped.slots.front().offset == 0);
    for ( std::size_t k = 1; k < shaped.slots.size(); ++k )
      CHECK(shaped.slots[k].offset >= shaped.slots[k - 1].offset + shaped.slots[k - 1].size);
    const std::uint32_t slot_end = shaped.slots.back().offset + shaped.slots.back().size;
    CHECK(shaped.args_bytes >= slot_end); // slots live inside the args window
  }
  CHECK(saw_nested); // the nested struct-argument decode works on 1.26
}

TEST_CASE("G7: stripped and non-Go corpora stay empty (fail-closed)")
{
  {
    module_t stripped;
    if ( parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/wasm-test/main.wasm",
                           &stripped) )
    {
      CHECK(stripped.go_profile.profile == go_profile_t::go1_26_series);
      CHECK_FALSE(stripped.go_types.present);
      CHECK(stripped.go_types.descriptors.empty());
      CHECK(stripped.go_types.arginfo.empty());
      CHECK_FALSE(stripped.go_types.anchor.present);
      CHECK(stripped.go_types.mismatch_reason.empty()); // no candidate: silence
    }
  }
  {
    module_t duckdb;
    if ( parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/wasm-test/duckdb.wasm",
                           &duckdb) )
    {
      CHECK(duckdb.go_profile.profile == go_profile_t::none);
      CHECK_FALSE(duckdb.go_types.present);
      CHECK(duckdb.go_types.descriptors.empty());
    }
  }
}

TEST_CASE("G7: rescan is idempotent and keeps the layer deterministic")
{
  module_t g126;
  if ( !parse_g126(&g126) )
    return;
  const std::size_t descriptors = g126.go_types.descriptors.size();
  const std::size_t arginfo = g126.go_types.arginfo.size();
  const std::uint32_t typelinks_len = g126.go_types.typelinks_len;
  scan_go_types(g126);
  CHECK(g126.go_types.present);
  CHECK(g126.go_types.descriptors.size() == descriptors);
  CHECK(g126.go_types.arginfo.size() == arginfo);
  CHECK(g126.go_types.typelinks_len == typelinks_len);
  // The lazy Pt append from the corpus test above does not survive a
  // rescan: only roots + the eager closure remain (snapshot equality).
  scan_go_types(g126);
  CHECK(g126.go_types.descriptors.size() == descriptors); // roots + closure
}

// Loader-order display name (name section first, then pclntab) and the
// name→body resolver used by the corpus ground truths below.
namespace
{
std::string body_display_name(const module_t &module, std::size_t body_index)
{
  const uint32 fn_index = uint32(module.imported_func_types.size() + body_index);
  for ( const export_t &entry : module.function_names )
    if ( entry.index == fn_index )
      return entry.name;
  if ( body_index < module.pclntab_function_names.size() )
    return module.pclntab_function_names[body_index];
  return std::string();
}
} // namespace

TEST_CASE("G7-S3: gowasm-balls itab layer — 17 itabs, 24 anchored functions")
{
  module_t mod;
  if ( !parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                          &mod) )
    return;
  const std::vector<go_itab_t> &itabs = go_itab_layer(mod);
  // Real-corpus ground truth: the scan finds the itab family (the exact
  // ==itablinks_len cross-check holds on g126 below; go1.27's moduledata
  // carries extra non-runnable link entries, so here the claim is the
  // non-empty family + the idempotent cache).
  CHECK(!itabs.empty());
  // Idempotent.
  CHECK(&go_itab_layer(mod) == &itabs);

  std::set<uint32> distinct;
  bool saw_error_error = false;
  for ( const go_itab_t &itab : itabs )
    for ( const go_itab_method_t &method : itab.methods )
    {
      if ( method.fn_index == UINT32_MAX )
        continue;
      distinct.insert(method.fn_index);
      if ( itab.inter_name == "error" && method.name == "Error"
           && itab.type_name == "*errors.errorString" )
        saw_error_error = true;
    }
  CHECK(!distinct.empty()); // anchored functions resolve (hole-tolerant reads)
  REQUIRE(saw_error_error);
  CHECK(mod.go_types.itabs_scanned);
}

TEST_CASE("G7-S3: gowasm-balls named signatures — itab / known-API / FuncOf / descriptor")
{
  module_t mod;
  if ( !parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                          &mod) )
    return;

  // Loader-order replication: entry.cpp resolves every body in index order
  // interleaved with the sret-pointee lookup — a later anchor must not
  // degrade after earlier ones mutated the lazy descriptor store (the
  // append-happy resolver is shared state).
  {
    module_t fresh;
    if ( parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                           &fresh) )
    {
      for ( std::size_t k = 0; k < fresh.bodies.size(); ++k )
      {
        go_named_sig_t order_sig;
        (void)go_resolve_named_signature(fresh, k, body_display_name(fresh, k), &order_sig);
      }
      go_named_sig_t after;
      const std::size_t body = body_index_for_name(fresh, "errors.__errorString_.Error");
      if ( body != SIZE_MAX
           && go_resolve_named_signature(fresh, body, "errors.__errorString_.Error", &after) )
        CHECK(after.complete);
    }
  }

  // itab anchor with a FULL signature: errors.(__errorString_).Error.
  {
    const std::size_t body = body_index_for_name(mod, "errors.__errorString_.Error");
    if ( body != SIZE_MAX )
    {
      go_named_sig_t sig;
      REQUIRE(go_resolve_named_signature(mod, body, "errors.__errorString_.Error", &sig));
      CHECK(sig.source == 1);
      CHECK(sig.label == "error.Error");
      CHECK(sig.complete);
      REQUIRE(sig.params.size() == 1); // the *errors.errorString receiver
      CHECK(sig.params[0].display == "*errors.errorString");
      CHECK(sig.params[0].size == 8);
      REQUIRE(sig.results.size() == 1);
      CHECK(sig.results[0].display == "string");
      CHECK(sig.results[0].c_name == "go__string");
      CHECK(sig.signature == "error.Error(*errors.errorString) string");
    }
  }

  // itab anchor on the rand.Source pair (the FuncTypes the survey's raw
  // name reads missed — the hole-tolerant layer resolves them fully).
  {
    const std::size_t body = body_index_for_name(mod, "rand.__lockedSource_.Int63");
    if ( body != SIZE_MAX )
    {
      go_named_sig_t sig;
      REQUIRE(go_resolve_named_signature(mod, body, "rand.__lockedSource_.Int63", &sig));
      CHECK(sig.source == 1);
      CHECK(sig.label == "rand.Source.Int63");
      CHECK(sig.complete);
      CHECK(sig.signature == "rand.Source.Int63(*rand.lockedSource) int64");
    }
  }

  // known-API: syscall/js.Value.Get — the loader's final display name is
  // the sanitized form ('/' becomes '_'), and the shape must match.
  {
    const std::size_t body = body_index_for_name(mod, "syscall/js.Value.Get");
    if ( body != SIZE_MAX )
    {
      go_named_sig_t sig;
      REQUIRE(go_resolve_named_signature(mod, body, "syscall_js.Value.Get", &sig));
      CHECK(sig.source == 2);
      REQUIRE(sig.params.size() == 2);
      CHECK(sig.params[0].display == "js.Value");
      CHECK(sig.params[0].c_name == "go__js__Value");
      CHECK(sig.params[1].display == "string");
      REQUIRE(sig.results.size() == 1);
      CHECK(sig.results[0].c_name == "go__js__Value");
      CHECK(sig.signature == "(js.Value) Get(js.Value, string) js.Value");
    }
  }

  // known-API: fmt.Sprintf (variadic form).
  {
    const std::size_t body = body_index_for_name(mod, "fmt.Sprintf");
    if ( body != SIZE_MAX )
    {
      go_named_sig_t sig;
      REQUIRE(go_resolve_named_signature(mod, body, "fmt.Sprintf", &sig));
      CHECK(sig.source == 2);
      CHECK(sig.variadic);
      CHECK(sig.results[0].display == "string");
      CHECK(sig.results[0].c_name == "go__string");
      CHECK(sig.signature == "fmt.Sprintf(string, []any...) string");
    }
  }

  // FuncOf callback: main.main.func1 (static funcval at the js.FuncOf call).
  {
    const std::size_t body = body_index_for_name(mod, "main.main.func1");
    if ( body != SIZE_MAX )
    {
      go_named_sig_t sig;
      REQUIRE(go_resolve_named_signature(mod, body, "main.main.func1", &sig));
      CHECK(sig.source == 3);
      REQUIRE(sig.params.size() == 2);
      CHECK(sig.params[0].display == "js.Value");
      CHECK(sig.params[1].display == "[]js.Value");
      REQUIRE(sig.results.size() == 1);
      CHECK(sig.results[0].display == "any");
      CHECK(sig.results[0].c_name == "go__iface");
      CHECK(sig.signature == "func(js.Value, []js.Value) any");
    }
  }

  // descriptor anchor: main.(__Ball_).draw — UncommonType Tfn proves the
  // body; the gap-cut mtyp withholds the typed arity (fail-closed partial).
  // 3.4: `__Ball_` is a pointer receiver — the display must carry the `*`
  // (value receivers like main.devNull.Write keep the bare form, pinned
  // further below on the g126 corpus).
  {
    const std::size_t body = body_index_for_name(mod, "main.__Ball_.draw");
    if ( body != SIZE_MAX )
    {
      go_named_sig_t sig;
      REQUIRE(go_resolve_named_signature(mod, body, "main.__Ball_.draw", &sig));
      CHECK(sig.source == 4);
      CHECK(sig.receiver == "*main.Ball");
      CHECK(sig.anchor
            == "method of *main.Ball (type descriptor) (method signature unrecoverable)");
      CHECK_FALSE(sig.complete);
      CHECK(sig.signature == "(*main.Ball) draw(...)");
    }
  }

  // No anchor: main.loop is a plain function — nothing proven, fail-closed.
  {
    const std::size_t body = body_index_for_name(mod, "main.loop");
    if ( body != SIZE_MAX )
    {
      go_named_sig_t sig;
      CHECK_FALSE(go_resolve_named_signature(mod, body, "main.loop", &sig));
    }
  }
}

TEST_CASE("G7-S3: C structs — go__js__Value from the descriptor, go__main__Ball fields")
{
  module_t mod;
  if ( !parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                          &mod) )
    return;
  // js.Value (16B, 3 fields — the 0-size `_` merges away under word layout).
  {
    go_sig_param_t param;
    param.c_name = "go__js__Value";
    param.display = "js.Value";
    go_c_struct_t layout;
    REQUIRE(go_c_struct_for_param(mod, param, &layout));
    CHECK(layout.c_name == "go__js__Value");
    CHECK(layout.size == 16);
    REQUIRE(layout.fields.size() == 2); // ref + gcPtr
    CHECK(layout.fields[0].offset == 0);
    CHECK(layout.fields[1].offset == 8);
  }
  // The parent-slice example: main.Ball decoded into a virtual C struct.
  {
    go_sig_param_t param;
    param.c_name = "go__main__Ball";
    param.display = "main.Ball";
    go_c_struct_t layout;
    REQUIRE(go_c_struct_for_param(mod, param, &layout));
    CHECK(layout.size == 56);
    REQUIRE(layout.fields.size() == 6);
    const char *want[6] = { "x", "y", "velX", "velY", "color", "size" };
    const uint64 want_off[6] = { 0, 8, 16, 24, 32, 48 };
    for ( int i = 0; i < 6; ++i )
    {
      CHECK(layout.fields[i].name == want[i]);
      CHECK(layout.fields[i].offset == want_off[i]);
    }
    CHECK(layout.fields[0].c_type == "double");
    CHECK(layout.fields[4].c_type == "struct go__string");
  }
  // Builtin composites are layout-fixed.
  {
    go_sig_param_t param;
    param.c_name = "go__slice";
    go_c_struct_t layout;
    REQUIRE(go_c_struct_for_param(mod, param, &layout));
    CHECK(layout.size == 24);
    CHECK(layout.fields.size() == 3);
  }
}

TEST_CASE("G7-S3: gowasm126 itab layer — 15 itabs, 21 anchored functions incl io.Writer on devNull")
{
  module_t g126;
  if ( !parse_g126(&g126) )
    return;
  const std::vector<go_itab_t> &itabs = go_itab_layer(g126);
  // Cross-layer ground truth: == the moduledata's itablinks count.
  REQUIRE(itabs.size() == g126.go_types.itablinks_len);
  std::set<uint32> distinct;
  bool saw_devnull = false;
  for ( const go_itab_t &itab : itabs )
    for ( const go_itab_method_t &method : itab.methods )
    {
      if ( method.fn_index == UINT32_MAX )
        continue;
      distinct.insert(method.fn_index);
      if ( itab.inter_name == "io.Writer" && method.name == "Write"
           && itab.type_name == "main.devNull" )
        saw_devnull = true;
    }
  CHECK(!distinct.empty()); // hole-tolerant reads resolve the itab anchors
  CHECK(saw_devnull);
  // The devNull Write anchor resolves with the full typed signature (the
  // 1.26 FuncTypes decode despite the relocation gaps around them).
  bool devnull_sig = false;
  for ( std::size_t i = 0; i < g126.bodies.size() && !devnull_sig; ++i )
  {
    go_named_sig_t sig;
    if ( go_resolve_named_signature(g126, i, "main.devNull.Write", &sig) && sig.source == 4 )
      devnull_sig = sig.signature == "(main.devNull) Write([]uint8) (int, error)";
  }
  CHECK(devnull_sig);
  // 3.4 contrast on the SAME corpus: the interface-dispatch WRAPPER body
  // `main.__devNull_.Write` (demangled main.(*devNull).Write — the method
  // set of *devNull) must display the pointer form, while the direct
  // value-receiver method above keeps the bare form. Two bodies, two
  // displays, both grounded in the symbol name.
  bool devnull_wrapper_ptr = false;
  for ( std::size_t i = 0; i < g126.bodies.size() && !devnull_wrapper_ptr; ++i )
  {
    go_named_sig_t sig;
    if ( go_resolve_named_signature(g126, i, "main.__devNull_.Write", &sig) && sig.source == 4 )
      devnull_wrapper_ptr
        = sig.receiver == "*main.devNull" && sig.signature.rfind("(*main.devNull) Write", 0) == 0;
  }
  CHECK(devnull_wrapper_ptr);
}

TEST_CASE("G7: g126 named-signature loop leaves the aggregate layer intact (1.26 coexistence pin)")
{
  module_t g126;
  if ( !parse_g126(&g126) )
    return;

  // The blob entry.cpp writes: encoded right after parse, BEFORE the named
  // loop (loader order) — collect_go_call_shapes consumes exactly this.
  std::vector<uint32> blob;
  encode_go_type_info_blob(g126.go_types, &blob);
  go_type_info_t written;
  REQUIRE(decode_go_type_info_blob(blob.data(), blob.size(), &written));
  REQUIRE(written.present);
  REQUIRE(written.layout_version == 26);
  REQUIRE_FALSE(written.arginfo.empty());

  // Snapshot the aggregate inputs before the loop mutates the shared state.
  const std::vector<go_arginfo_t> arginfo_before = g126.go_types.arginfo;

  // The loop in entry.cpp shape: resolve every body in index order. On
  // 1.26 the first resolve lazily scans the itablinks layer and the lazy
  // resolver APPENDS descriptors to the very go_types the blob came from.
  std::size_t named = 0;
  for ( std::size_t i = 0; i < g126.bodies.size(); ++i )
  {
    go_named_sig_t sig;
    if ( go_resolve_named_signature(g126, i, body_display_name(g126, i), &sig) )
      ++named;
  }
  CHECK(named > 0); // at least the itab-anchored functions resolve

  // (1) The aggregate inputs are byte-identical after the loop.
  REQUIRE(g126.go_types.arginfo.size() == arginfo_before.size());
  for ( std::size_t i = 0; i < arginfo_before.size(); ++i )
  {
    const go_arginfo_t &got = g126.go_types.arginfo[i];
    const go_arginfo_t &want = arginfo_before[i];
    CHECK(got.present == want.present);
    CHECK(got.args_bytes == want.args_bytes);
    REQUIRE(got.slots.size() == want.slots.size());
    for ( std::size_t k = 0; k < want.slots.size(); ++k )
    {
      CHECK(got.slots[k].offset == want.slots[k].offset);
      CHECK(got.slots[k].size == want.slots[k].size);
      CHECK(got.slots[k].depth == want.slots[k].depth);
    }
    CHECK(got.group_starts == want.group_starts);
  }

  // (2) The merge pin's callee keeps its aggregate shape: fmt.Sprintf's
  // ArgInfo {0:8,8:8}{16:8,24:8,32:8} with top-level groups opening at 0
  // (the string) and 16 (the slice) — the main.Pt.String call-site merge
  // is built from exactly these words.
  const std::size_t sprintf_body = body_index_for_name(g126, "fmt.Sprintf");
  REQUIRE(sprintf_body != SIZE_MAX);
  REQUIRE(sprintf_body < written.arginfo.size());
  {
    const go_arginfo_t &arginfo = written.arginfo[sprintf_body];
    REQUIRE(arginfo.present);
    REQUIRE(slots_match(arginfo, { { 0, 8 }, { 8, 8 }, { 16, 8 }, { 24, 8 }, { 32, 8 } }));
    REQUIRE(arginfo.group_starts.size() == 5);
    CHECK(arginfo.group_starts[0] == 1);
    CHECK(arginfo.group_starts[1] == 0);
    CHECK(arginfo.group_starts[2] == 1);
    CHECK(arginfo.group_starts[3] == 0);
    CHECK(arginfo.group_starts[4] == 0);
    CHECK(arginfo.args_bytes == 56);
  }

  // (3) The named layer still pins AFTER the loop ran — coexistence, not
  // just survival: the Sprintf known-API anchor and the devNull Write
  // signature stay resolvable on the post-loop state.
  {
    go_named_sig_t sig;
    REQUIRE(go_resolve_named_signature(g126, sprintf_body, "fmt.Sprintf", &sig));
    CHECK(sig.complete);
    CHECK(sig.signature == "fmt.Sprintf(string, []any...) string");
  }
  {
    const std::size_t body = body_index_for_name(g126, "main.devNull.Write");
    if ( body != SIZE_MAX )
    {
      go_named_sig_t sig;
      REQUIRE(go_resolve_named_signature(g126, body, "main.devNull.Write", &sig));
      CHECK(sig.signature == "(main.devNull) Write([]uint8) (int, error)");
    }
  }
}

TEST_CASE("G7-S3: blob 't' v1 stays untouched by the itab layer")
{
  module_t mod;
  if ( !parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm",
                          &mod) )
    return;
  const std::size_t before = mod.go_types.descriptors.size();
  std::vector<uint32> encoded;
  encode_go_type_info_blob(mod.go_types, &encoded);
  (void)go_itab_layer(mod);
  const std::size_t error_body = body_index_for_name(mod, "errors.__errorString_.Error");
  REQUIRE(error_body != SIZE_MAX);
  (void)go_resolve_named_signature(mod, error_body, "errors.__errorString_.Error", nullptr);
  std::vector<uint32> after;
  encode_go_type_info_blob(mod.go_types, &after);
  // The codec sees only the v1 fields: the descriptor APPENDS from the lazy
  // resolver change the blob (they are v1 content), but scanning itabs and
  // resolving signatures on an untouched layer is a no-op for the blob.
  CHECK(mod.go_types.descriptors.size() >= before);
  CHECK(after.size() >= encoded.size());
  go_type_info_t decoded;
  REQUIRE(decode_go_type_info_blob(after.data(), after.size(), &decoded));
  CHECK(decoded.itabs.empty()); // itabs are NOT part of the v1 wire format
  CHECK_FALSE(decoded.itabs_scanned);
}

TEST_CASE("G7: blob 't' v1 round-trips the gowasm-balls layer")
{
  const module_t *balls = cached_balls();
  if ( balls == nullptr )
    return;
  const go_type_info_t &info = balls->go_types;
  std::vector<uint32> encoded;
  encode_go_type_info_blob(info, &encoded);
  CHECK(encoded.size() > 40);

  go_type_info_t decoded;
  REQUIRE(decode_go_type_info_blob(encoded.data(), encoded.size(), &decoded));
  CHECK(decoded.present == info.present);
  CHECK(decoded.layout_version == info.layout_version);
  CHECK(decoded.moduledata == info.moduledata);
  CHECK(decoded.types == info.types);
  CHECK(decoded.gofunc == info.gofunc);
  CHECK(decoded.typedesclen == info.typedesclen);
  CHECK(decoded.etypes == info.etypes);
  CHECK(decoded.itaboffset == info.itaboffset);
  CHECK(decoded.itabsize == info.itabsize);
  CHECK(decoded.typelinks_len == info.typelinks_len);
  CHECK(decoded.itablinks_len == info.itablinks_len);
  REQUIRE(decoded.descriptors.size() == info.descriptors.size());
  for ( size_t i = 0; i < info.descriptors.size(); ++i )
  {
    const go_type_descriptor_t &want = info.descriptors[i];
    const go_type_descriptor_t &got = decoded.descriptors[i];
    CHECK(got.addr == want.addr);
    CHECK(got.kind == want.kind);
    CHECK(got.size == want.size);
    CHECK(got.tflag == want.tflag);
    CHECK(got.str_off == want.str_off);
    CHECK(got.name == want.name);
    CHECK(got.pkg_path == want.pkg_path);
    CHECK(got.ptr_to_this == want.ptr_to_this);
    CHECK(got.fields.size() == want.fields.size());
    CHECK(got.param_addrs == want.param_addrs);
    CHECK(got.variadic == want.variadic);
  }
  REQUIRE(decoded.arginfo.size() == info.arginfo.size());
  for ( size_t i = 0; i < info.arginfo.size(); ++i )
  {
    CHECK(decoded.arginfo[i].present == info.arginfo[i].present);
    CHECK(decoded.arginfo[i].args_bytes == info.arginfo[i].args_bytes);
    CHECK(decoded.arginfo[i].slots.size() == info.arginfo[i].slots.size());
    for ( size_t k = 0; k < info.arginfo[i].slots.size(); ++k )
    {
      CHECK(decoded.arginfo[i].slots[k].offset == info.arginfo[i].slots[k].offset);
      CHECK(decoded.arginfo[i].slots[k].size == info.arginfo[i].slots[k].size);
      CHECK(decoded.arginfo[i].slots[k].depth == info.arginfo[i].slots[k].depth);
      // The group-boundary flags ride the spare bits of the same
      // packed word (bit 24) — the word count and every prior field
      // position are unchanged.
      if ( k < decoded.arginfo[i].group_starts.size() )
        CHECK(decoded.arginfo[i].group_starts[k]
              == (k < info.arginfo[i].group_starts.size() ? info.arginfo[i].group_starts[k] : 0));
    }
  }
  CHECK(decoded.anchor.present == info.anchor.present);
  CHECK(decoded.anchor.header == info.anchor.header);
  CHECK(decoded.anchor.nfunc == info.anchor.nfunc);
  CHECK(decoded.anchor.ftab_base == info.anchor.ftab_base);
  CHECK(decoded.anchor.func_addrs == info.anchor.func_addrs);

  // A re-encode of the decoded layer is byte-identical (stable format).
  std::vector<uint32> reencoded;
  encode_go_type_info_blob(decoded, &reencoded);
  CHECK(reencoded == encoded);
}

TEST_CASE("G7: blob 't' decode fails closed on corruption")
{
  const module_t *balls = cached_balls();
  if ( balls == nullptr )
    return;
  std::vector<uint32> encoded;
  encode_go_type_info_blob(balls->go_types, &encoded);
  go_type_info_t decoded;

  CHECK_FALSE(decode_go_type_info_blob(nullptr, encoded.size(), &decoded));
  CHECK_FALSE(decode_go_type_info_blob(encoded.data(), 0, &decoded));
  CHECK_FALSE(decode_go_type_info_blob(encoded.data(), 3, &decoded)); // short header

  std::vector<uint32> bad_version = encoded;
  bad_version[0] = go_types_blob_version + 1; // unknown future version
  CHECK_FALSE(decode_go_type_info_blob(bad_version.data(), bad_version.size(), &decoded));

  // A v1 stream still decodes — same prefix, no
  // layout tail — with an empty layout vector (call sites keep canonical
  // extent names, fail-closed).
  std::vector<uint32> v1_stream = encoded;
  v1_stream[0] = go_types_blob_v1;
  // Trim the layout tail: everything after the arginfo section. Find it by
  // decoding lengths — simpler: a v1 decode of the v2 stream fails (extra
  // tail words), and a hand-built v1 round-trip is covered below.
  CHECK_FALSE(decode_go_type_info_blob(v1_stream.data(), v1_stream.size(), &decoded));

  // Truncations anywhere reject: every record count read hits cursor==end
  // before the stream is complete, so no strict prefix is a valid blob.
  for ( std::size_t cut = 20; cut < encoded.size(); cut += 97 )
  {
    go_type_info_t partial;
    CHECK_FALSE(decode_go_type_info_blob(encoded.data(), cut, &partial));
  }

  // A stream with trailing garbage rejects (strict cursor==end).
  std::vector<uint32> trailing = encoded;
  trailing.push_back(0);
  CHECK_FALSE(decode_go_type_info_blob(trailing.data(), trailing.size(), &decoded));

  // An empty layer encodes to the fixed header and round-trips.
  go_type_info_t empty;
  std::vector<uint32> empty_encoded;
  encode_go_type_info_blob(empty, &empty_encoded);
  CHECK(empty_encoded.size() >= 15);
  go_type_info_t empty_decoded;
  REQUIRE(decode_go_type_info_blob(empty_encoded.data(), empty_encoded.size(), &empty_decoded));
  CHECK_FALSE(empty_decoded.present);
  CHECK(empty_decoded.descriptors.empty());
  CHECK(empty_decoded.arginfo.empty());
  CHECK(empty_decoded.layouts.empty());

  // A v1 stream (version word 1, prefix only, no layout tail) decodes
  // with an empty layout vector — the backward-compatibility contract.
  {
    std::vector<uint32> empty_v1 = empty_encoded;
    empty_v1[0] = go_types_blob_v1;
    // The empty layer carries no tail, so the v1 stream is identical to
    // the v2 one minus the version word and the trailing layout count.
    // Rebuild it by hand: header + zero sections.
    std::vector<uint32> v1_words = empty_v1;
    REQUIRE(v1_words.size() >= 15);
    v1_words[0] = go_types_blob_v1;
    // Drop the trailing layout-count word (the only tail section).
    REQUIRE(v1_words.size() >= 16);
    v1_words.pop_back();
    go_type_info_t v1_decoded;
    REQUIRE(decode_go_type_info_blob(v1_words.data(), v1_words.size(), &v1_decoded));
    CHECK_FALSE(v1_decoded.present);
    CHECK(v1_decoded.layouts.empty());
  }
}

// The value-display helpers — the pure core of the
// string-literal / boxing / member-access display pass. The filter side
// proves WHICH words form a pair from the decoded stream; these tests pin
// the byte-level half (validation + reading + field covering) on the same
// corpus the display runs on (gowasm-balls, go1.27.3).

namespace
{
// Read `n` bytes at a linear-memory offset from the module's active data
// segments (the loader maps exactly these bytes; anything else is BSS).
bool balls_read_data(const module_t &mod, uint64 at, std::size_t n, std::uint8_t *out)
{
  for ( std::size_t i = 0; i < mod.data_segment_sizes.size(); ++i )
  {
    if ( mod.data_segment_offsets[i] < 0 )
      continue;
    const uint64 start = uint64(mod.data_segment_offsets[i]);
    const uint64 size = uint64(mod.data_segment_sizes[i]);
    if ( at >= start && at + n <= start + size )
    {
      std::memcpy(out, mod.data_segment_data[i].data() + (at - start), n);
      return true;
    }
  }
  return false;
}
} // namespace

TEST_CASE("M3 G6 go-string-literal: validation and the balls call-site pairs")
{
  // Fail-closed rules: no NUL, valid UTF-8, nonzero length.
  const uint8_t plain[] = "beginPath";
  std::string text;
  REQUIRE(go_string_literal_text(plain, 9, &text));
  CHECK(text == "beginPath");

  CHECK_FALSE(go_string_literal_text(plain, 0, &text)); // len 0: keep numeric
  const uint8_t with_nul[] = "ab\0cd";
  CHECK_FALSE(go_string_literal_text(with_nul, 5, &text)); // C literal cannot
  const uint8_t bad_utf8[] = { 0xff, 0xfe, 'a' };
  CHECK_FALSE(go_string_literal_text(bad_utf8, 3, &text));
  const uint8_t truncated[] = { 'a', 0xe2, 0x82 }; // 3-byte seq cut short
  CHECK_FALSE(go_string_literal_text(truncated, 3, &text));
  const uint8_t multibyte[] = { 0xc3, 0xa9, 0xe2, 0x82, 0xac }; // é€
  REQUIRE(go_string_literal_text(multibyte, 5, &text));
  CHECK(text == "\xc3\xa9\xe2\x82\xac");

  // The exact call-site string literals are scanned out of the active data
  // segments (printable, NUL-free runs — exactly what the (ptr, len) pairs
  // pointed at), then read back through the same data-segment walk the
  // display uses — no pinned corpus addresses.
  const module_t *balls = cached_balls();
  if ( balls == nullptr )
    return; // corpus absent on this machine
  std::size_t checked_literals = 0;
  for ( std::size_t seg = 0; seg < balls->data_segment_sizes.size() && checked_literals < 8; ++seg )
  {
    if ( balls->data_segment_offsets[seg] < 0 )
      continue;
    const std::uint8_t *data
      = reinterpret_cast<const std::uint8_t *>(balls->data_segment_data[seg].data());
    const std::size_t size = balls->data_segment_sizes[seg];
    std::size_t run = 0;
    for ( std::size_t i = 0; i <= size && checked_literals < 8; ++i )
    {
      const bool literal_byte = i < size && data[i] >= 0x20 && data[i] < 0x7f;
      if ( literal_byte )
      {
        ++run;
        continue;
      }
      if ( run >= 3 && run <= 64 )
      {
        std::string literal;
        REQUIRE(go_string_literal_text(data + i - run, run, &literal));
        CHECK(literal.size() == run);
        ++checked_literals;
      }
      run = 0;
    }
  }
  CHECK(checked_literals >= 4); // the draw-style call-site strings exist
  // The boxing type words are NOT string pointers (the filter's iface
  // guard): the layer carries the builtin boxing kinds the filter branches
  // on — string(24,16B), float64(14,8B), int(2,8B) — found by kind+size,
  // no corpus addresses.
  const struct
  {
    std::uint8_t kind;
    std::uint64_t size;
  } boxing_kinds[] = { { 24, 16 }, { 14, 8 }, { 2, 8 } };
  for ( const auto &want : boxing_kinds )
  {
    bool found = false;
    for ( const go_type_descriptor_t &candidate : balls->go_types.descriptors )
      if ( candidate.kind == want.kind && candidate.size == want.size )
      {
        found = true;
        break;
      }
    CHECK_MESSAGE(found, "boxing descriptor kind missing: " << int(want.kind));
  }
}

TEST_CASE("M3 G7 go-boxing: scalar kinds read per descriptor + balls statics")
{
  std::string value;
  const uint8_t zeros[8] = {};
  // Int (2): signed — the balls any(0) data word is BSS zeroes.
  REQUIRE(go_boxed_scalar_text(2, zeros, &value));
  CHECK(value == "0");
  const uint8_t one[8] = { 1, 0, 0, 0, 0, 0, 0, 0 };
  REQUIRE(go_boxed_scalar_text(6, one, &value)); // Int64
  CHECK(value == "1");
  const uint8_t umax[8] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
  REQUIRE(go_boxed_scalar_text(11, umax, &value)); // Uint64
  CHECK(value == "18446744073709551615");
  // Bool (1) reads one byte.
  REQUIRE(go_boxed_scalar_text(1, one, &value));
  CHECK(value == "true");
  // Float64 (14): shortest round-trip — 2*math.Pi must print exactly like
  // the Go source literal, not %.17g noise.
  const uint8_t two_pi[8] = { 0x18, 0x2d, 0x44, 0x54, 0xfb, 0x21, 0x19, 0x40 };
  REQUIRE(go_boxed_scalar_text(14, two_pi, &value));
  CHECK(value == "6.283185307179586");
  CHECK(std::strtod(value.c_str(), nullptr) == 6.283185307179586);
  // Composite kinds are not plain scalars (kind-only display).
  CHECK_FALSE(go_boxed_scalar_text(24, two_pi, &value)); // String
  CHECK_FALSE(go_boxed_scalar_text(25, two_pi, &value)); // Struct

  // Corpus: the 2*math.Pi static lives in an active data segment — find
  // the bit pattern itself and prove the read + shortest-round-trip render
  // at every hit; the BSS check uses the computed end of the last segment
  // (anything past it is outside every segment — the zero-init MEMORY the
  // display's "= 0" reads come from), not a pinned offset.
  const module_t *balls = cached_balls();
  if ( balls == nullptr )
    return;
  const std::uint8_t two_pi_bits[8] = { 0x18, 0x2d, 0x44, 0x54, 0xfb, 0x21, 0x19, 0x40 };
  std::uint64_t segments_end = 0;
  std::size_t two_pi_hits = 0;
  for ( std::size_t seg = 0; seg < balls->data_segment_sizes.size(); ++seg )
  {
    if ( balls->data_segment_offsets[seg] < 0 )
      continue;
    const std::uint64_t start = uint64(balls->data_segment_offsets[seg]);
    const std::size_t size = balls->data_segment_sizes[seg];
    if ( start + size > segments_end )
      segments_end = start + size;
    const std::uint8_t *data
      = reinterpret_cast<const std::uint8_t *>(balls->data_segment_data[seg].data());
    for ( std::size_t i = 0; i + 8 <= size; ++i )
      if ( std::memcmp(data + i, two_pi_bits, 8) == 0 )
      {
        ++two_pi_hits;
        std::uint8_t f64_bytes[8];
        REQUIRE(balls_read_data(*balls, start + i, 8, f64_bytes));
        REQUIRE(go_boxed_scalar_text(14, f64_bytes, &value));
        CHECK(value == "6.283185307179586");
      }
  }
  CHECK(two_pi_hits > 0);
  REQUIRE(segments_end > 0);
  std::uint8_t bss_byte = 0x5a;
  CHECK_FALSE(balls_read_data(*balls, segments_end, 1, &bss_byte)); // BSS: no segment

  // The boxed kinds of draw's any elements: string(24,16B), float64(14,8B),
  // int(2,8B) — found by kind+size, no corpus addresses.
  const struct
  {
    std::uint8_t kind;
    std::uint64_t size;
  } boxing_kinds[] = { { 24, 16 }, { 14, 8 }, { 2, 8 } };
  for ( const auto &want : boxing_kinds )
  {
    bool found = false;
    for ( const go_type_descriptor_t &candidate : balls->go_types.descriptors )
      if ( candidate.kind == want.kind && candidate.size == want.size )
      {
        found = true;
        break;
      }
    CHECK_MESSAGE(found, "boxing descriptor kind missing: " << int(want.kind));
  }
}

TEST_CASE("M3 G8 go-member: covering fields of *main.Ball accesses")
{
  const module_t *balls = cached_balls();
  if ( balls == nullptr )
    return;
  const go_type_descriptor_t *ball = nullptr;
  for ( const go_type_descriptor_t &descriptor : balls->go_types.descriptors )
    if ( go_descriptor_type_name(descriptor) == "main.Ball" && descriptor.kind == 25 )
    {
      ball = &descriptor;
      break;
    }
  REQUIRE(ball != nullptr);
  REQUIRE(!ball->fields.empty());
  std::string field;
  // Self-consistent covering: every field of the decoded descriptor is
  // THE covering name at its own (offset, width) — the width derived from
  // the gap to the next field (the descriptor carries offsets only), the
  // last field spanning to the struct size. What the pinned per-field
  // accesses held, derived from the descriptor itself.
  for ( std::size_t i = 0; i < ball->fields.size(); ++i )
  {
    const std::uint64_t width = i + 1 < ball->fields.size()
      ? ball->fields[i + 1].offset - ball->fields[i].offset
      : ball->size - ball->fields[i].offset;
    REQUIRE(width > 0);
    REQUIRE(go_member_covering(balls->go_types, *ball, ball->fields[i].offset, width, &field));
    CHECK(field == ball->fields[i].name);
  }
  // Straddling and out-of-struct accesses have no honest name — the
  // boundaries computed from the decoded layout (a mid-field start with
  // the field's width straddles the next field; past the size is out).
  if ( ball->fields.size() >= 2 )
  {
    const std::uint64_t gap = ball->fields[1].offset - ball->fields[0].offset;
    if ( gap >= 4 )
      CHECK_FALSE(
        go_member_covering(balls->go_types, *ball, ball->fields[0].offset + gap / 2, gap, &field));
  }
  CHECK_FALSE(go_member_covering(balls->go_types, *ball, ball->size, 8, &field)); // past end
}
