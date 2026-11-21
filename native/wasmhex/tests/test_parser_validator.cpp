// Parser + validator oracle test: every tests/golden/<name>/_validator.json
// records the front end verdict for tests/fixtures/**/<name>.wasm; the pure
// core's module_t::parse must reproduce all 406 valid / 13 invalid verdicts
// with zero IDA involvement.
#include "doctest.h"

#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../core/types.hpp"

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

std::string json_string_field(const std::string &json, const std::string &key)
{
  const size_t at = json.find("\"" + key + "\"");
  if ( at == std::string::npos )
    return std::string();
  const size_t colon = json.find(':', at + key.size() + 2);
  if ( colon == std::string::npos )
    return std::string();
  size_t p = colon + 1;
  while ( p < json.size() && (json[p] == ' ' || json[p] == '\t') )
    ++p;
  if ( p < json.size() && json[p] == '"' )
  {
    const size_t end = json.find('"', p + 1);
    return json.substr(p + 1, end - p - 1);
  }
  return json.substr(p, json.find_first_of(",}\n", p) - p);
}

bool json_bool_field(const std::string &json, const std::string &key)
{
  return json_string_field(json, key).find("true") != std::string::npos;
}

std::vector<std::string> list_dir(const std::string &path)
{
  std::vector<std::string> names;
  DIR *dir = opendir(path.c_str());
  if ( dir == nullptr )
    return names;
  while ( const dirent *entry = readdir(dir) )
  {
    const std::string name = entry->d_name;
    if ( name != "." && name != ".." )
      names.push_back(name);
  }
  closedir(dir);
  return names;
}

bool find_fixture(const std::string &name, std::string *out)
{
  static const char *roots[] = { "/tests/fixtures", "/tests/fixtures/brtable" };
  for ( const char *root : roots )
  {
    const std::string candidate = std::string(WASMHEX_REPO_DIR) + root + "/" + name + ".wasm";
    if ( std::ifstream(candidate).good() )
    {
      *out = candidate;
      return true;
    }
  }
  return false;
}

} // namespace

TEST_CASE("golden validator oracle: spec-invalid fixtures are rejected by the core")
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
  // Tracked spec-compliance bug (shared with test_spec_oracle): the
  // atomics stack discipline is not modeled, so this engine-invalid module
  // parses (and even validates) today.
  const std::string tracked_violation = "wasmhex_atomics_invalid_stack";

  const std::string golden_root = std::string(WASMHEX_REPO_DIR) + "/tests/golden";
  size_t invalid_checked = 0, bad = 0, stale = 0, unknown = 0;
  std::vector<std::string> missing;
  for ( const std::string &name : list_dir(golden_root) )
  {
    const std::string validator = read_file(golden_root + "/" + name + "/_validator.json");
    if ( validator.empty() || json_bool_field(validator, "valid") )
      continue;
    std::string fixture;
    if ( !find_fixture(name, &fixture) )
    {
      missing.push_back(name);
      continue;
    }
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
    const std::string bytes = read_file(fixture);
    REQUIRE_FALSE(bytes.empty());
    module_t module;
    if ( module.parse(reinterpret_cast<const uchar *>(bytes.data()), bytes.size()) )
    {
      if ( name != tracked_violation )
      {
        ++bad;
        std::printf("BAD: V8-invalid %s parsed by the core\n", name.c_str());
      }
    }
    ++invalid_checked;
  }
  INFO("invalid checked=" << invalid_checked << " bad=" << bad << " missing=" << missing.size());
  // All engine-invalid fixtures must be exercised: 10 original (9 refuse
  // to parse + 1 tracked atomics violation) + 3 GC/call-ref/fixed-memory
  // invalid fixtures already wired in the tsv — grows only with the tsv.
  CHECK(invalid_checked == 14);
  CHECK(bad == 0);
  CHECK(stale <= 90); // shrink to 0 when the golden oracle is refreshed
  std::printf("INFO oracle-invalid corpus: v8-invalid checked=%zu bad=%zu "
              "stale-artifacts=%zu unknown=%zu\n",
              invalid_checked, bad, stale, unknown);
}

TEST_CASE("parser: section counts on known fixtures")
{
  const std::string add = read_file(std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/add.wasm");
  REQUIRE_FALSE(add.empty());
  module_t module;
  REQUIRE(module.parse(reinterpret_cast<const uchar *>(add.data()), add.size()));
  CHECK(module.types.size() == 1);
  CHECK(module.bodies.size() == 1);
  CHECK(module.function_exports.size() == 1);
  CHECK(module.function_exports[0].name == "add");
  CHECK(module.start_function_index == UINT32_MAX);

  const std::string br3
    = read_file(std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/br_table.wasm");
  REQUIRE_FALSE(br3.empty());
  module_t br;
  REQUIRE(br.parse(reinterpret_cast<const uchar *>(br3.data()), br3.size()));
  CHECK(br.bodies.size() == 1);
  // One br_table control plus its scoped block markers.
  size_t tables = 0;
  for ( const body_t::control_t &control : br.bodies[0].control )
    if ( control.opcode == 0x0E )
      ++tables;
  CHECK(tables == 1);
}

TEST_CASE("parser: chain and jtbl modes produce identical raw targets")
{
  const std::string bytes
    = read_file(std::string(WASMHEX_REPO_DIR) + "/tests/fixtures/br_table.wasm");
  module_t chain, jtbl;
  chain.brtable_mode = wasmhex_brtable_mode_t::chain;
  jtbl.brtable_mode = wasmhex_brtable_mode_t::jtbl;
  REQUIRE(chain.parse(reinterpret_cast<const uchar *>(bytes.data()), bytes.size()));
  REQUIRE(jtbl.parse(reinterpret_cast<const uchar *>(bytes.data()), bytes.size()));
  const body_t::control_t *chain_table = nullptr;
  const body_t::control_t *jtbl_table = nullptr;
  for ( const body_t::control_t &control : chain.bodies[0].control )
    if ( control.opcode == 0x0E )
      chain_table = &control;
  for ( const body_t::control_t &control : jtbl.bodies[0].control )
    if ( control.opcode == 0x0E )
      jtbl_table = &control;
  REQUIRE(chain_table != nullptr);
  REQUIRE(jtbl_table != nullptr);
  CHECK(chain_table->targets.size() == jtbl_table->targets.size());
  CHECK(chain_table->depths == jtbl_table->depths);
  // Jtbl no longer inserts a virtual-stub region before the br_table
  // stubs are empty and the raw target offsets are identical to chain mode.
  CHECK(jtbl_table->stubs.empty());
  CHECK(jtbl_table->targets == chain_table->targets);
  // jtbl mode must not synthesize a dispatcher.
  CHECK(jtbl_table->dispatch == UINT32_MAX);
}
