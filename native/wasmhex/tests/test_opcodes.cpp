#include "doctest.h"

#include <stdexcept>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../core/binary/instructions.hpp"

namespace
{
using namespace wasmhex::opcodes;

struct row_t
{
  const char *name;
  int byte;
  int sub;
  wasmhex::opcodes::imm_kind_t imm;
  const char *mnem;
  const char *flags;
};

std::vector<row_t> collect()
{
  std::vector<row_t> rows;
#define ROW(name, byte, sub, imm, mnem, flags)                                                     \
  rows.push_back({ #name, byte, sub, imm, mnem, #flags });
  WASMHEX_ITYPES(ROW)
#undef ROW
  return rows;
}

std::vector<std::string> oracle_lines()
{
  std::ifstream in(std::string(WASMHEX_REPO_DIR) + "/native/wasmhex/tests/itypes_oracle.txt");
  std::vector<std::string> out;
  std::string line;
  while ( std::getline(in, line) )
    if ( line.size() > 3 && line[0] != 'E' && line[0] != 'T' )
      out.push_back(line);
  return out;
}

} // namespace

TEST_CASE("opcode table: row count matches the pre-F1 itype enum")
{
  const auto rows = collect();
  CHECK(rows.size() == 245);
}

TEST_CASE("opcode table: encodings are unique except the documented alias")
{
  const auto rows = collect();
  for ( size_t i = 0; i < rows.size(); ++i )
    for ( size_t j = i + 1; j < rows.size(); ++j )
    {
      const bool same_byte = rows[i].byte == rows[j].byte && rows[i].byte != 0;
      const bool same_sub = rows[i].sub == rows[j].sub;
      const bool prefix = rows[i].byte == 0xFB || rows[i].byte == 0xFC || rows[i].byte == 0xFD
        || rows[i].byte == 0xFE;
      if ( same_byte && (!prefix || same_sub) )
      {
        // Only the 0x10 call/call.unwind alias may share an encoding.
        CHECK(
          (rows[i].byte == 0x10
           && (std::string(rows[i].name) == "call" || std::string(rows[i].name) == "call_unwind")
           && (std::string(rows[j].name) == "call" || std::string(rows[j].name) == "call_unwind")));
      }
    }
}

TEST_CASE("opcode table: representative immediate kinds")
{
  const auto rows = collect();
  const auto find = [&rows](const char *name) -> const row_t &
  {
    for ( const row_t &row : rows )
      if ( std::string(row.name) == name )
        return row;
    FAIL("missing row ", name);
    throw std::logic_error("unreachable"); // FAIL throws; keeps -Wreturn-type quiet
  };
  CHECK(find("unreachable").imm == wasmhex::opcodes::IMM_NONE);
  CHECK(find("i32_add").byte == 0x6A);
  CHECK(find("local_get").imm == wasmhex::opcodes::IMM_ULEB);
  CHECK(find("local_get").byte == 0x20);
  CHECK(find("br_table").imm == wasmhex::opcodes::IMM_BR_TABLE);
  CHECK(find("block").imm == wasmhex::opcodes::IMM_BLOCKTYPE);
  CHECK(find("i32_const").imm == wasmhex::opcodes::IMM_I32_CONST);
  CHECK(find("f64_const").imm == wasmhex::opcodes::IMM_F64_CONST);
  CHECK(find("i32_load").imm == wasmhex::opcodes::IMM_MEMARG);
  CHECK(find("i32_load").byte == 0x28);
  CHECK(find("memory_size").imm == wasmhex::opcodes::IMM_MEM_SIZEGROW);
  CHECK(find("ref_null").imm == wasmhex::opcodes::IMM_REF_NULL);
  CHECK(find("select_typed").imm == wasmhex::opcodes::IMM_SELECT_TYPED);
  CHECK(find("call").imm == wasmhex::opcodes::IMM_CALL);
  CHECK(find("call_indirect").imm == wasmhex::opcodes::IMM_CALL_INDIRECT);
  CHECK(find("memory_init").imm == wasmhex::opcodes::IMM_FC_SUB);
  CHECK(find("memory_init").sub == 8);
  CHECK(find("table_size").sub == 16);
  CHECK(find("v128_load").imm == wasmhex::opcodes::IMM_FD_SUB);
  CHECK(find("v128_store64_lane").sub == 91);
  CHECK(find("atomic").imm == wasmhex::opcodes::IMM_FE_SUB);
  CHECK(find("ref_i31").imm == wasmhex::opcodes::IMM_FB_SUB);
  CHECK(find("branch_goto").byte == 0xFF);
  CHECK(find("try").imm == wasmhex::opcodes::IMM_SPECIAL);
  CHECK(find("ref_func").imm == wasmhex::opcodes::IMM_SPECIAL);
}

TEST_CASE("opcode table: names/mnemonics/flags match the pre-F1 oracle")
{
  const auto rows = collect();
  const auto oracle = oracle_lines();
  REQUIRE(oracle.size() >= rows.size());
  for ( size_t i = 0; i < rows.size(); ++i )
  {
    // oracle line format: "  <idx> <name> <mnemonic> <flags>"; the null
    // sentinel row has no mnemonic column (the trailing 0 is its flags).
    if ( std::string(rows[i].name) == "null" )
      continue;
    // oracle line format: "  <idx> <name> <mnemonic> <flags>"
    std::istringstream line(oracle[i]);
    int index;
    std::string name, mnem, flags;
    line >> index >> name >> mnem >> flags;
    INFO("row " << i << " " << rows[i].name << " vs " << name);
    CHECK(rows[i].name == name);
    CHECK(std::string(rows[i].mnem) == mnem);
    const std::string table_flags = rows[i].flags;
    const std::string expected = flags == "0" ? "0" : flags;
    CHECK(table_flags.find(expected) != std::string::npos);
  }
}
