// Unit pins for the constant-address evidence walk
// (core/analysis/mem_drefs.cpp). Every pin is fail-closed: an access whose
// address is not provably constant must NOT appear in the records.
#include "../core/analysis/mem_drefs.hpp"

#include "doctest.h"

#include "corpus_guard.hpp"

#include <fstream>
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
} // namespace

TEST_CASE("H3 dref walk: synthetic const-address load/store shapes")
{
  // tests/fixtures/h3_dref_walk.wat — built by the fixtures target:
  //   i32.const 0x100; i32.load offset=4        -> read at 0x104
  //   i32.const 0x200; i32.const 8; i32.add; i64.store offset=0 -> write at 0x208
  //   local.get 0; i32.load offset=0            -> NOT proven (dynamic base)
  //   i32.const 0x300; drop                      -> no access
  //   global.get 0; i32.load                     -> NOT proven (dynamic base)
  module_t module;
  REQUIRE(parse_module_file(std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/h3_dref_walk.wasm",
                            &module));
  std::vector<mem_dref_t> mem;
  std::vector<global_ref_t> globals;
  REQUIRE(const_mem_drefs(module, &mem, &globals) == module.bodies.size());
  REQUIRE(mem.size() == 2);
  CHECK(mem[0].address == 0x104);
  CHECK(mem[0].store == false);
  CHECK(mem[1].address == 0x208);
  CHECK(mem[1].store == true);
  // global.get sites: the fixture body reads $base then $sp — both
  // references are proven even though the $base load address is not.
  REQUIRE(globals.size() == 2);
  CHECK(globals[0].global_index == 1); // $base
  CHECK(globals[1].global_index == 0); // $sp
}

TEST_CASE("H3 dref walk: balls corpus — receiver pair and global references proven")
{
  WASMHEX_REQUIRE_CORPUS_FILE("/wasm-test/gowasm-balls/main.wasm");
  module_t balls;
  REQUIRE(
    parse_module_file(std::string(WASMHEX_REPO_DIR) + "/wasm-test/gowasm-balls/main.wasm", &balls));
  std::vector<mem_dref_t> mem;
  std::vector<global_ref_t> globals;
  REQUIRE(const_mem_drefs(balls, &mem, &globals) == balls.bodies.size());
  // The receiver-global walk (test_go_profile.cpp "js.Value
  // receiver-global evidence walk") proves the js.Value receiver slots;
  // the dref walk must observe a read inside every proven 16-byte slot —
  // cross-checked from the same module's layers, not a pinned address.
  std::vector<std::uint64_t> receiver;
  REQUIRE(go_jsvalue_receiver_globals(balls, &receiver));
  CHECK(!receiver.empty());
  for ( std::uint64_t base : receiver )
  {
    bool saw_word = false;
    for ( const mem_dref_t &record : mem )
      if ( !record.store && record.address >= base && record.address < base + 16 )
        saw_word = true;
    CHECK(saw_word);
  }
  bool saw_any_store = false;
  for ( const mem_dref_t &record : mem )
    saw_any_store = saw_any_store || record.store;
  CHECK(saw_any_store);    // data-slot writes exist on this corpus (constants,
                           // string bytes, itabs) — a walk that proves only
                           // reads would be missing half the references
  CHECK(!globals.empty()); // every `global.get SP` site lands here
}
