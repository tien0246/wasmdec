// C string args: unit pins for the callee-side
// pointer-parameter walk (core/analysis/param_pointers.cpp). Every pin is
// fail-closed: a parameter whose pointer use is not provable must NOT be
// marked, and a constant argument at an unproven position must NOT record.
#include "../core/analysis/param_pointers.hpp"

#include "doctest.h"

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

TEST_CASE("libc pointer table: positions only, names exact")
{
  CHECK(libc_pointer_mask("sscanf") == 0x3);   // str + format
  CHECK(libc_pointer_mask("snprintf") == 0x5); // str + format (size is not)
  CHECK(libc_pointer_mask("memcpy") == 0x3);   // dest + src
  CHECK(libc_pointer_mask("memset") == 0x1);   // dest only
  CHECK(libc_pointer_mask("strlen") == 0x1);
  CHECK(libc_pointer_mask("strcmp") == 0x3);
  CHECK(libc_pointer_mask("MD5_Init") == 0); // never guessed
  CHECK(libc_pointer_mask("not_a_libc_function") == 0);
}

TEST_CASE("pointer walk: synthetic spill/forward shapes")
{
  // tests/fixtures/wasmhex_c_string_args.wat — built by the fixtures target.
  //   $sink (body 0): param 0 is a store address base  -> bit 0 proven
  //   $hex2 (body 1): param 0 spilled to the shadow stack ([sp-16+12])
  //                   and reloaded into sscanf arg 0 (a table pointer
  //                   position) -> bit 0 proven transitively; param 1 only
  //                   feeds the varargs area (arg 2, NOT a pointer
  //                   position) -> stays unproven
  //   $main (body 2): no pointer params of its own
  module_t module;
  REQUIRE(parse_module_file(
    std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/wasmhex_c_string_args.wasm", &module));
  REQUIRE(module.bodies.size() == 3);
  // Function 0 is the imported sscanf: str + format are pointer positions.
  std::vector<std::uint64_t> table_masks(module.imported_func_types.size() + module.bodies.size());
  table_masks[0] = libc_pointer_mask("sscanf");
  const param_pointer_result_t result = param_pointer_walk(module, table_masks, 0x10000);
  REQUIRE(result.proven_param_masks.size() == 3);
  CHECK(result.proven_param_masks[0] == 0x1); // $sink: param 0
  CHECK(result.proven_param_masks[1] == 0x1); // $hex2: param 0 only
  CHECK(result.proven_param_masks[2] == 0x0); // $main: nothing

  // Constant pointer arguments recorded with the converged masks, in body
  // order ($hex2 before $main): hex2's own sscanf format (0x400), then
  // main's calls — sink arg0 (0x900), sscanf arg0 (0x404) + arg1 (0x400),
  // hex2 arg0 (0x420, proven by the fixpoint). sscanf arg2 (0x940) and
  // hex2 arg1 (0x940) must NOT appear anywhere.
  std::vector<std::uint64_t> addresses;
  for ( const pointer_arg_record_t &record : result.arg_records )
    addresses.push_back(record.address);
  REQUIRE(addresses.size() == 5);
  CHECK(addresses[0] == 0x400);
  CHECK(addresses[1] == 0x900);
  CHECK(addresses[2] == 0x404);
  CHECK(addresses[3] == 0x400);
  CHECK(addresses[4] == 0x420);
  bool leaked_varargs = false;
  for ( const pointer_arg_record_t &record : result.arg_records )
    if ( record.address == 0x940 )
      leaked_varargs = true;
  CHECK(!leaked_varargs);
}

TEST_CASE("pointer walk: memory_limit 0 records nothing")
{
  module_t module;
  REQUIRE(parse_module_file(
    std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/wasmhex_c_string_args.wasm", &module));
  std::vector<std::uint64_t> table_masks(module.imported_func_types.size() + module.bodies.size());
  table_masks[0] = libc_pointer_mask("sscanf");
  const param_pointer_result_t result = param_pointer_walk(module, table_masks, 0);
  CHECK(result.arg_records.empty());
  // The masks themselves are memory-independent proofs.
  CHECK(result.proven_param_masks[0] == 0x1);
}
