// C++ port of the former Python fixture generators
// (pure stdlib, no python). Byte-identical outputs; the craft-based groups
// always rewrite deterministically, the clang-based groups keep the python
// mtime-idempotency (skip when outputs are newer than the inputs).
// Usage (one subcommand per former script):
//   gen_fixtures component-multi   <fixtures_dir>
//   gen_fixtures validator-states  <out_dir>
//   gen_fixtures pclntab-names     <fixtures_dir>
//   gen_fixtures brtable-wat       <brtable_dir>
//   gen_fixtures differential      <gen_dir> <fixtures_dir>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

using bytes = std::vector<std::uint8_t>;

bytes operator+(const bytes &a, const bytes &b)
{
  bytes out(a);
  out.insert(out.end(), b.begin(), b.end());
  return out;
}

bytes &operator+=(bytes &a, const bytes &b)
{
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

bytes from_literal(const char *text)
{
  return bytes(text, text + std::strlen(text));
}

// strlen-based literals cannot carry a leading NUL — the wasm magics are
// spelled explicitly (hex escapes inside string literals are also greedy:
// "\x00asm" would parse as \x00a + "sm").
// strlen-based literals cannot carry a NUL anywhere (leading or inner),
// so every payload that may contain 0x00 is spelled as an explicit list.
bytes bhex(std::initializer_list<std::uint8_t> items)
{
  return bytes(items);
}

bytes magic_module()
{
  return { 0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00 };
}

bytes magic_component()
{
  return { 0x00, 0x61, 0x73, 0x6D, 0x0D, 0x00, 0x01, 0x00 };
}

bytes uleb(std::uint64_t value)
{
  bytes out;
  for ( ;; )
  {
    const std::uint8_t byte = value & 0x7F;
    value >>= 7;
    if ( value != 0 )
      out.push_back(byte | 0x80);
    else
    {
      out.push_back(byte);
      return out;
    }
  }
}

bytes sleb(std::int64_t value)
{
  bytes out;
  for ( ;; )
  {
    const std::uint8_t byte = value & 0x7F;
    value >>= 7;
    if ( (value == 0 && (byte & 0x40) == 0) || (value == -1 && (byte & 0x40) != 0) )
    {
      out.push_back(byte);
      return out;
    }
    out.push_back(byte | 0x80);
  }
}

bytes section(std::uint8_t sid, const bytes &payload)
{
  bytes out;
  out.push_back(sid);
  out += uleb(payload.size());
  out += payload;
  return out;
}

bytes module_bytes(const std::vector<bytes> &sections)
{
  bytes out = magic_module();
  for ( const bytes &part : sections )
    out += part;
  return out;
}

void write_file(const std::filesystem::path &path, const bytes &data)
{
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
}

bool write_file_if_stale(const std::filesystem::path &path, const bytes &data)
{
  std::error_code ec;
  const auto stamp = std::filesystem::last_write_time(path, ec);
  if ( !ec )
  {
    std::ifstream in(path, std::ios::binary);
    const bytes current((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if ( current == data )
      return false; // already identical — do not touch the committed file
  }
  write_file(path, data);
  return true;
}

bytes core_module(const char *export_name)
{
  const bytes types = section(1, uleb(1) + bhex({ 0x60, 0x00, 0x00 }));
  const bytes funcs = section(3, uleb(1) + uleb(0));
  const std::string name(export_name);
  const bytes exports = section(
    7, uleb(1) + uleb(name.size()) + bytes(name.begin(), name.end()) + bhex({ 0x00 }) + uleb(0));
  const bytes code = section(10, uleb(1) + uleb(2) + bhex({ 0x00, 0x0B }));
  return magic_module() + types + funcs + exports + code;
}

int gen_component_multi(const std::filesystem::path &fixtures)
{
  const bytes data
    = magic_component() + section(1, core_module("mod0_fn")) + section(1, core_module("mod1_fn"));
  const auto target = fixtures / "component_multi_core.wasm";
  write_file_if_stale(target, data);
  std::printf("WASMHEX_C7_GEN %s bytes=%zu\n", target.filename().c_str(), data.size());
  return 0;
}

bytes type_section(const std::vector<std::pair<bytes, bytes>> &types)
{
  bytes body = uleb(types.size());
  for ( const auto &[params, results] : types )
  {
    body.push_back(0x60);
    body += uleb(params.size());
    body += params;
    body += uleb(results.size());
    body += results;
  }
  return section(1, body);
}

bytes func_section(const std::vector<std::uint64_t> &indices)
{
  bytes body = uleb(indices.size());
  for ( std::uint64_t index : indices )
    body += uleb(index);
  return section(3, body);
}

bytes code_section(const std::vector<bytes> &bodies)
{
  bytes body = uleb(bodies.size());
  for ( const bytes &code : bodies )
  {
    body += uleb(code.size());
    body += code;
  }
  return section(10, body);
}

bytes build_a5()
{
  const bytes types = type_section({ { from_literal("\x7f"), {} } });
  const bytes funcs = func_section({ 0 });
  const bytes mem = section(5, uleb(1) + uleb(0) + from_literal("\x01"));
  const bytes body = uleb(0) + bhex({ 0x41, 0x00 }) + bhex({ 0xFD, 0x00 }) + uleb(4) + uleb(0)
    + from_literal("\x1a\x0b");
  return module_bytes({ types, funcs, mem, code_section({ body }) });
}

bytes build_a6_bad()
{
  const bytes types = type_section({ { {}, {} } });
  const bytes funcs = func_section({ 0 });
  const bytes locals_ = uleb(1) + uleb(1) + from_literal("\x64\x70");
  const bytes body = locals_ + bhex({ 0x20, 0x00 }) + from_literal("\x1a") + from_literal("\x0b");
  return module_bytes({ types, funcs, code_section({ body }) });
}

bytes build_a6_ok()
{
  const bytes types = type_section({ { {}, {} } });
  const bytes funcs = func_section({ 0, 0 });
  const bytes table = section(4, uleb(1) + from_literal("\x70\x01\x01\x01"));
  const bytes elem = section(9,
                             uleb(1) + from_literal("\x02") + uleb(0) + bhex({ 0x41, 0x00, 0x0B })
                               + bhex({ 0x00 }) + uleb(1) + uleb(0));
  const bytes locals_ = uleb(1) + uleb(1) + from_literal("\x64\x70");
  const bytes body = locals_ + bhex({ 0xD2, 0x00 }) + bhex({ 0x21, 0x00 }) + bhex({ 0x20, 0x00 })
    + from_literal("\x1a") + from_literal("\x0b");
  return module_bytes(
    { types, funcs, table, elem, code_section({ uleb(0) + from_literal("\x0b"), body }) });
}

bytes build_a7(std::uint64_t count)
{
  const bytes types = type_section({ { {}, {} } });
  const bytes funcs = func_section({ 0 });
  const bytes locals_ = uleb(1) + uleb(count) + from_literal("\x7f");
  const bytes body = locals_ + from_literal("\x0b");
  return module_bytes({ types, funcs, code_section({ body }) });
}

bytes build_a8()
{
  const bytes types = type_section({ { from_literal("\x7f"), from_literal("\x7f") }, { {}, {} } });
  const bytes funcs = func_section({ 0, 1 });
  const bytes table = section(4, uleb(1) + from_literal("\x70\x05") + uleb(1) + uleb(1));
  const bytes elem = section(9,
                             uleb(1) + from_literal("\x02") + uleb(0) + bhex({ 0x42, 0x00, 0x0B })
                               + bhex({ 0x00 }) + uleb(1) + uleb(0));
  const bytes target_body = uleb(0) + bhex({ 0x20, 0x00, 0x0B });
  const bytes caller_locals = uleb(1) + uleb(1) + from_literal("\x7e");
  const bytes caller_body = caller_locals + bhex({ 0x42, 0x00 }) + bhex({ 0x21, 0x00 })
    + from_literal("\x41\x07") + bhex({ 0x20, 0x00 }) + from_literal("\x11") + uleb(0) + uleb(0)
    + from_literal("\x1a") + from_literal("\x0b");
  return module_bytes({ types, funcs, table, elem, code_section({ target_body, caller_body }) });
}

bytes build_s5()
{
  const bytes types = type_section({ { {}, {} } });
  const bytes funcs = func_section({ 0 });
  const bytes locals_ = uleb(1) + uleb(1) + from_literal("\x63\x70");
  const bytes body = locals_ + bhex({ 0x20, 0x00 }) + from_literal("\x1a") + from_literal("\x0b");
  return module_bytes({ types, funcs, code_section({ body }) });
}

int gen_validator_states(const std::filesystem::path &out_dir)
{
  std::filesystem::create_directories(out_dir);
  const std::vector<std::pair<const char *, bytes>> fixtures = {
    { "s5_typed_nullable_ref.wasm", build_s5() },
    { "a5_unsupported_opcode.wasm", build_a5() },
    { "a6_local_unassigned_bad.wasm", build_a6_bad() },
    { "a6_local_assigned_ok.wasm", build_a6_ok() },
    { "a7_local_count_cap.wasm", build_a7(50001) },
    { "a7_local_count_bomb.wasm", build_a7(20000000) },
    { "a8_table64_const_index.wasm", build_a8() },
  };
  for ( const auto &[name, data] : fixtures )
  {
    write_file_if_stale(out_dir / name, data);
    std::printf("WASMHEX_A58_GEN %s bytes=%zu\n", name, data.size());
  }
  return 0;
}

void append_le32(bytes &out, std::uint32_t value)
{
  for ( int i = 0; i < 4; ++i )
    out.push_back(std::uint8_t(value >> (8 * i)));
}

void append_le64(bytes &out, std::uint64_t value)
{
  for ( int i = 0; i < 8; ++i )
    out.push_back(std::uint8_t(value >> (8 * i)));
}

int gen_pclntab_names(const std::filesystem::path &fixtures)
{
  const std::vector<std::string> names
    = { "go:buildid", "runtime.mini", "main.helper", "main.main" };
  std::uint64_t name_off[4] = {};
  std::uint64_t at = 0;
  for ( size_t i = 0; i < names.size(); ++i )
  {
    name_off[i] = at;
    at += names[i].size() + 1;
  }
  bytes funcnametab;
  for ( size_t i = 0; i < names.size(); ++i )
  {
    funcnametab += bytes(names[i].begin(), names[i].end());
    funcnametab.push_back(0x00);
  }

  const std::uint64_t fname_off = 0x48;
  const std::uint64_t functab_off = fname_off + funcnametab.size();
  const std::uint32_t entries[4][2]
    = { { 0x1000, 0 }, { 0x1001, 1 }, { 0x1002, 2 }, { 0x1003, 3 } };
  bytes table;
  for ( int k = 0; k < 4; ++k )
  {
    append_le32(table, entries[k][0]);
    append_le32(table, 5 * 8 + k * 8);
  }
  append_le32(table, 0x1004);
  append_le32(table, 0);
  bytes funcs_blob;
  for ( int k = 0; k < 4; ++k )
  {
    append_le32(funcs_blob, 0x1000 + std::uint32_t(k));
    append_le32(funcs_blob, std::uint32_t(name_off[k]));
  }

  bytes pclntab(fname_off, 0x00);
  pclntab += funcnametab;
  const std::uint64_t functab_start = pclntab.size();
  pclntab += table;
  pclntab += funcs_blob;

  bytes header;
  append_le32(header, 0xFFFFFFF1);
  header.push_back(0);
  header.push_back(0);
  header.push_back(1);
  header.push_back(8);
  append_le64(header, 3);
  append_le64(header, 1);
  append_le64(header, 0);
  append_le64(header, fname_off);
  append_le64(header, functab_start);
  append_le64(header, functab_start);
  append_le64(header, functab_start);
  append_le64(header, functab_start);
  if ( header.size() != fname_off )
  {
    std::fprintf(stderr, "pclntab header size %zu != %llu\n", header.size(),
                 static_cast<unsigned long long>(fname_off));
    return 1;
  }
  bytes blob = header;
  blob += bytes(pclntab.begin() + header.size(), pclntab.end());

  const bytes types = section(1, uleb(1) + bhex({ 0x60, 0x00, 0x00 }));
  const bytes memory = section(5, uleb(1) + bhex({ 0x00 }) + uleb(1));
  const bytes funcs = section(3, uleb(3) + uleb(0) + uleb(0) + uleb(0));
  const std::string exported("go_exported");
  const bytes exports = section(
    7, uleb(1) + uleb(11) + bytes(exported.begin(), exported.end()) + bhex({ 0x00 }) + uleb(0));
  const bytes code = section(10,
                             uleb(3) + uleb(2) + bhex({ 0x00, 0x0B }) + uleb(2)
                               + bhex({ 0x00, 0x0B }) + uleb(2) + bhex({ 0x00, 0x0B }));
  const std::uint64_t dest = 0xC160;
  const bytes pad = bhex({ 0x70, 0x61, 0x64, 0x00 });
  const bytes data
    = section(11,
              uleb(2) + uleb(0) + from_literal("\x41") + sleb(std::int64_t(dest))
                + from_literal("\x0b") + uleb(blob.size()) + blob + uleb(0) + from_literal("\x41")
                + sleb(0x2000) + from_literal("\x0b") + uleb(4) + pad);
  const bytes payload = module_bytes({ types, funcs, memory, exports, code, data });
  const auto out = fixtures / "g6_pclntab_names.wasm";
  write_file_if_stale(out, payload);
  std::printf("WASMHEX_G6_GEN %s bytes=%zu\n", out.filename().c_str(), payload.size());
  return 0;
}

std::string repeat_join(const std::string &token, int count)
{
  std::string out;
  for ( int i = 0; i < count; ++i )
  {
    if ( i != 0 )
      out += " ";
    out += token;
  }
  return out;
}

std::string same_target(int count)
{
  const std::string labels = repeat_join("$even", count);
  return "(module\n"
         "  (func (export \"dispatch\") (param i32) (result i32)\n"
         "    (local $r i32)\n"
         "    (block $out\n"
         "      (block $odd\n"
         "        (block $even\n"
         "          local.get 0\n"
         "          (br_table "
    + labels
    + " $odd)\n"
      "        )\n"
      "        i32.const 11 local.set $r br $out\n"
      "      )\n"
      "      i32.const 7 local.set $r\n"
      "    )\n"
      "    local.get $r))\n";
}

std::string all_same(int count)
{
  const std::string labels = repeat_join("$one", count + 1);
  return "(module\n"
         "  (func (export \"dispatch\") (param i32) (result i32)\n"
         "    (block $one\n"
         "      local.get 0\n"
         "      (br_table "
    + labels
    + ")\n"
      "    )\n"
      "    i32.const 42))\n";
}

std::string large(int count)
{
  std::vector<std::string> labels;
  for ( int index = 0; index < count; ++index )
  {
    if ( index % 16 == 15 )
      labels.push_back("$outer");
    else
      labels.push_back(index % 2 == 0 ? "$even" : "$odd");
  }
  labels.push_back("$outer");
  std::string joined;
  for ( size_t i = 0; i < labels.size(); ++i )
  {
    if ( i != 0 )
      joined += " ";
    joined += labels[i];
  }
  return "(module\n"
         "  (func (export \"dispatch\") (param i32) (result i32)\n"
         "    (local $r i32)\n"
         "    (block $outer\n"
         "      (block $odd\n"
         "        (block $even\n"
         "          local.get 0\n"
         "          (br_table "
    + joined
    + ")\n"
      "        )\n"
      "        i32.const 100 local.set $r br $outer\n"
      "      )\n"
      "      i32.const 101 local.set $r br $outer\n"
      "    )\n"
      "    local.get $r))\n";
}

std::string depth_per_case(int n)
{
  std::vector<std::string> lines;
  lines.push_back("(module");
  lines.push_back("  (func (export \"dispatch\") (param i32) (result i32)");
  lines.push_back("    (local $r i32)");
  for ( int k = 0; k < n; ++k )
    lines.push_back(std::string(2 * (2 + k), ' ') + "block $b" + std::to_string(k));
  std::string labels;
  for ( int i = 0; i < n; ++i )
  {
    if ( i != 0 )
      labels += " ";
    labels += "$b" + std::to_string(n - 1 - i);
  }
  labels += " $b" + std::to_string(n - 1);
  lines.push_back(std::string(2 * (2 + n), ' ') + "local.get 0");
  lines.push_back(std::string(2 * (2 + n), ' ') + "br_table " + labels);
  for ( int j = n - 1; j > 0; --j )
  {
    lines.push_back(std::string(2 * (2 + j), ' ') + "end");
    lines.push_back(std::string(2 * (2 + j), ' ') + "i32.const " + std::to_string(n - 1 - j)
                    + " local.set $r");
    lines.push_back(std::string(2 * (2 + j), ' ') + "br $b0");
  }
  lines.push_back("    end");
  lines.push_back("    local.get $r");
  lines.push_back("  ))");
  std::string out;
  for ( size_t i = 0; i < lines.size(); ++i )
  {
    if ( i != 0 )
      out += "\n";
    out += lines[i];
  }
  return out + "\n";
}

const char *const SMALL = "(module\n"
                          "  (func (export \"dispatch\") (param i32) (result i32)\n"
                          "    (local $r i32)\n"
                          "    (block $out\n"
                          "      (block $a\n"
                          "        (block $b\n"
                          "          (block $c\n"
                          "            local.get 0\n"
                          "            (br_table $c $b $a $out)\n"
                          "          )\n"
                          "          i32.const 10 local.set $r br $out\n"
                          "        )\n"
                          "        i32.const 20 local.set $r br $out\n"
                          "      )\n"
                          "      i32.const 30 local.set $r\n"
                          "    )\n"
                          "    local.get $r))\n";

const char *const DEFAULT_DUP = "(module\n"
                                "  (func (export \"dispatch\") (param i32) (result i32)\n"
                                "    (local $r i32)\n"
                                "    (block $out\n"
                                "      (block $x\n"
                                "        (block $y\n"
                                "          local.get 0\n"
                                "          (br_table $x $y $x $x)\n"
                                "        )\n"
                                "        i32.const 20 local.set $r br $out\n"
                                "      )\n"
                                "      i32.const 10 local.set $r\n"
                                "    )\n"
                                "    local.get $r))\n";

const char *const NESTED = "(module\n"
                           "  (func (export \"dispatch\") (param i32) (result i32)\n"
                           "    (local $r i32)\n"
                           "    (block $out\n"
                           "      (loop $loop\n"
                           "        (block $a\n"
                           "          (block $b\n"
                           "            (block $c\n"
                           "              (if (i32.ge_s (local.get 0) (i32.const 100))\n"
                           "                (then\n"
                           "                  local.get 0\n"
                           "                  i32.const 100\n"
                           "                  i32.sub\n"
                           "                  local.set 0))\n"
                           "              local.get 0\n"
                           "              (br_table $c $b $a $out)\n"
                           "            )\n"
                           "            i32.const 10 local.set $r br $out\n"
                           "          )\n"
                           "          i32.const 20 local.set $r br $out\n"
                           "        )\n"
                           "        i32.const 30 local.set $r br $out\n"
                           "      )\n"
                           "    )\n"
                           "    local.get $r))\n";

const char *const DEPTHS = "(module\n"
                           "  (func (export \"dispatch\") (param i32) (result i32)\n"
                           "    (local $r i32)\n"
                           "    (block $l0\n"
                           "      (block $l1\n"
                           "        (block $l2\n"
                           "          (block $l3\n"
                           "            (block $l4\n"
                           "              local.get 0\n"
                           "              (br_table $l4 $l3 $l2 $l1 $l0 $l4)\n"
                           "            )\n"
                           "            i32.const 4 local.set $r br $l0\n"
                           "          )\n"
                           "          i32.const 3 local.set $r br $l0\n"
                           "        )\n"
                           "        i32.const 2 local.set $r br $l0\n"
                           "      )\n"
                           "      i32.const 1 local.set $r\n"
                           "    )\n"
                           "    local.get $r))\n";

const char *const UNREACHABLE = "(module\n"
                                "  (func (export \"dispatch\") (param i32) (result i32)\n"
                                "    (local $r i32)\n"
                                "    (block $out\n"
                                "      (block $a\n"
                                "        (block $b\n"
                                "          (block $c\n"
                                "            (block $dead\n"
                                "              br $a\n"
                                "              local.get 0\n"
                                "              (br_table $dead $c $b $out)\n"
                                "            )\n"
                                "            i32.const 10 local.set $r br $out\n"
                                "          )\n"
                                "          i32.const 20 local.set $r br $out\n"
                                "        )\n"
                                "        i32.const 30 local.set $r br $out\n"
                                "      )\n"
                                "    )\n"
                                "    local.get $r))\n";

const char *const PAYLOAD = "(module\n"
                            "  (func (export \"dispatch\") (param i32) (result i32)\n"
                            "    (block $out (result i32)\n"
                            "      (block $a (result i32)\n"
                            "        (block $b (result i32)\n"
                            "          (block $c (result i32)\n"
                            "            (i32.add (local.get 0) (i32.const 100))\n"
                            "            (local.get 0)\n"
                            "            (br_table $c $b $a $out)\n"
                            "          )\n"
                            "          i32.const 1000 i32.add\n"
                            "        )\n"
                            "        i32.const 2000 i32.add\n"
                            "      )\n"
                            "      i32.const 3000 i32.add\n"
                            "    )))\n";

int gen_brtable_wat(const std::filesystem::path &brtable_dir)
{
  std::filesystem::create_directories(brtable_dir);
  const std::vector<std::pair<const char *, std::string>> fixtures = {
    { "bt_small.wat", SMALL },
    { "bt_same_target.wat", same_target(8) },
    { "bt_all_same.wat", all_same(8) },
    { "bt_default_dup.wat", DEFAULT_DUP },
    { "bt_large.wat", large(256) },
    { "bt_nested.wat", NESTED },
    { "bt_depths.wat", DEPTHS },
    { "bt_unreachable.wat", UNREACHABLE },
    { "bt_payload.wat", PAYLOAD },
    { "bt_depth_per_case_64.wat", depth_per_case(64) },
    { "bt_depth_per_case_256.wat", depth_per_case(256) },
  };
  std::vector<std::string> written;
  for ( const auto &[name, source] : fixtures )
  {
    const bytes data(source.begin(), source.end());
    if ( write_file_if_stale(brtable_dir / name, data) )
      written.push_back(name);
  }
  if ( written.empty() )
    std::printf("WASMHEX_BRTABLE_GEN_UP_TO_DATE\n");
  else
    std::printf("WASMHEX_BRTABLE_GEN_OK %zu files\n", written.size());
  return 0;
}

bool file_at_least(const std::filesystem::path &path, const std::filesystem::file_time_type &stamp)
{
  std::error_code ec;
  const auto own = std::filesystem::last_write_time(path, ec);
  return !ec && own >= stamp;
}

int gen_differential(const std::filesystem::path &gen_dir, const std::filesystem::path &fixtures)
{
  const auto source = gen_dir / "differential.c";
  std::error_code ec;
  const auto newest = std::filesystem::last_write_time(source, ec);
  const std::filesystem::path outputs[2]
    = { fixtures / "differential_o0.wasm", fixtures / "differential_o2.wasm" };
  if ( file_at_least(outputs[0], newest) && file_at_least(outputs[1], newest) )
  {
    std::printf("WASMHEX_DIFF_GEN_UP_TO_DATE\n");
    return 0;
  }
  if ( std::system("command -v clang >/dev/null 2>&1") != 0 ) // NOLINT(bugprone-command-processor)
  {
    std::printf("WASMHEX_DIFF_GEN_SKIPPED (clang not found)\n");
    return 0;
  }
  const char *const levels[2] = { "O0", "O2" };
  for ( int i = 0; i < 2; ++i )
  {
    const std::string command = std::string("clang --target=wasm32 -") + levels[i]
      + " -nostdlib -Wl,--no-entry -Wl,--export-all -Wl,--strip-all " + source.string() + " -o "
      + outputs[i].string();
    if ( std::system(command.c_str()) != 0 ) // NOLINT(bugprone-command-processor)
    {
      std::fprintf(stderr, "gen_fixtures: clang failed for -%s\n", levels[i]);
      return 1;
    }
  }
  std::printf("WASMHEX_DIFF_GEN_OK differential_o0.wasm differential_o2.wasm\n");
  return 0;
}

bytes vec_bytes(const std::vector<bytes> &items)
{
  bytes out = uleb(items.size());
  for ( const bytes &item : items )
    out += item;
  return out;
}

bytes simd_v128_const_zero()
{
  bytes out = { 0xFD, 0x0C };
  out.insert(out.end(), 16, 0x00);
  return out;
}

bytes simd_i32_zero()
{
  return { 0x41, 0x00 };
}

bytes simd_i64_zero()
{
  return { 0x42, 0x00 };
}

bytes simd_f32_zero()
{
  return { 0x43, 0x00, 0x00, 0x00, 0x00 };
}

bytes simd_f64_zero()
{
  bytes out = { 0x44 };
  out.insert(out.end(), 8, 0x00);
  return out;
}

bytes simd_memarg(std::uint64_t align, std::uint64_t offset = 0)
{
  return uleb(align) + uleb(offset);
}

bytes simd_lane_load(std::uint64_t sub, std::uint64_t align, std::uint8_t lane)
{
  return simd_i32_zero() + simd_v128_const_zero() + bhex({ 0xFD }) + uleb(sub) + simd_memarg(align)
    + bhex({ lane });
}

bytes simd_lane_store(std::uint64_t sub, std::uint64_t align, std::uint8_t lane)
{
  return simd_i32_zero() + simd_v128_const_zero() + bhex({ 0xFD }) + uleb(sub) + simd_memarg(align)
    + bhex({ lane });
}

bytes simd_extract(std::uint64_t sub, std::uint8_t lane)
{
  return simd_v128_const_zero() + bhex({ 0xFD }) + uleb(sub) + bhex({ lane });
}

bytes simd_replace(std::uint64_t sub, std::uint8_t lane, const bytes &push)
{
  return simd_v128_const_zero() + push + bhex({ 0xFD }) + uleb(sub) + bhex({ lane });
}

int gen_simd_lane_bounds(const std::filesystem::path &fixtures)
{
  struct SimdCase
  {
    const char *name;
    bytes body;
    bool needs_drop;
  };
  const bytes v128_zero = simd_v128_const_zero();
  const bytes i32_zero = simd_i32_zero();
  const std::vector<SimdCase> cases = {
    { "load8_lane_16", simd_lane_load(0x54, 0, 16), true },
    { "load16_lane_8", simd_lane_load(0x55, 1, 8), true },
    { "load32_lane_4", simd_lane_load(0x56, 2, 4), true },
    { "load64_lane_2", simd_lane_load(0x57, 3, 2), true },
    { "store8_lane_16", simd_lane_store(0x58, 0, 16), false },
    { "store16_lane_8", simd_lane_store(0x59, 1, 8), false },
    { "store32_lane_4", simd_lane_store(0x5A, 2, 4), false },
    { "store64_lane_2", simd_lane_store(0x5B, 3, 2), false },
    { "extract_i8x16_16", simd_extract(0x15, 16), true },
    { "extract_i16x8_8", simd_extract(0x18, 8), true },
    { "extract_i32x4_4", simd_extract(0x1B, 4), true },
    { "extract_i64x2_2", simd_extract(0x1D, 2), true },
    { "extract_f32x4_4", simd_extract(0x1F, 4), true },
    { "extract_f64x2_2", simd_extract(0x21, 2), true },
    { "replace_i8x16_16", simd_replace(0x17, 16, i32_zero), true },
    { "replace_i16x8_8", simd_replace(0x1A, 8, i32_zero), true },
    { "replace_i32x4_4", simd_replace(0x1C, 4, i32_zero), true },
    { "replace_i64x2_2", simd_replace(0x1E, 2, simd_i64_zero()), true },
    { "replace_f32x4_4", simd_replace(0x20, 4, simd_f32_zero()), true },
    { "replace_f64x2_2", simd_replace(0x22, 2, simd_f64_zero()), true },
    { "shuffle_32",
      v128_zero + v128_zero + bhex({ 0xFD, 0x0D })
        + bhex({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 32 }),
      true },
    { "align_v128_load_5", i32_zero + bhex({ 0xFD, 0x00 }) + simd_memarg(5), true },
    { "align_load8_lane_1", simd_lane_load(0x54, 1, 0), true },
    { "align_load64_lane_4", simd_lane_load(0x57, 4, 0), true },
    { "align_load32_zero_3", i32_zero + bhex({ 0xFD, 0x5C }) + simd_memarg(3), true },
    { "sub_0x9a", v128_zero + bhex({ 0xFD }) + uleb(0x9A), false },
    { "sub_0xa2", v128_zero + bhex({ 0xFD }) + uleb(0xA2), false },
    { "sub_0xe2", v128_zero + bhex({ 0xFD }) + uleb(0xE2), false },
  };

  std::vector<bytes> func_indices(cases.size(), uleb(0));
  std::vector<bytes> export_entries;
  std::vector<bytes> body_entries;
  export_entries.reserve(cases.size());
  body_entries.reserve(cases.size());
  for ( size_t i = 0; i < cases.size(); ++i )
  {
    const std::string name(cases[i].name);
    export_entries.push_back(uleb(name.size()) + bytes(name.begin(), name.end()) + bhex({ 0x00 })
                             + uleb(i));
    bytes code = bhex({ 0x00 }) + cases[i].body;
    if ( cases[i].needs_drop )
      code.push_back(0x1A);
    code.push_back(0x0B);
    body_entries.push_back(uleb(code.size()) + code);
  }

  const bytes wasm = magic_module() + section(1, vec_bytes({ bhex({ 0x60, 0x00, 0x00 }) }))
    + section(3, vec_bytes(func_indices)) + section(5, bhex({ 0x01, 0x00, 0x01 }))
    + section(7, vec_bytes(export_entries)) + section(10, vec_bytes(body_entries))
    + section(11, bhex({ 0x00 }));

  const auto target
    = (fixtures.extension() == ".wasm") ? fixtures : (fixtures / "simd_lane_bounds_invalid.wasm");
  write_file_if_stale(target, wasm);
  std::printf("WASMHEX_SIMD_LANE_BOUNDS_GEN %s bytes=%zu cases=%zu\n", target.filename().c_str(),
              wasm.size(), cases.size());
  return 0;
}

int gen_typed_ref_unsupported_gc(const std::filesystem::path &fixtures)
{
  const bytes wasm = {
    0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01, 0x5F, 0x01, 0x7F, 0x00,
  };
  const char *const names[2]
    = { "wasmhex_typed_ref_unsupported_gc.wasm", "build_wasmhex_typed_ref_unsupported_gc.wasm" };
  for ( const char *name : names )
  {
    const auto target = fixtures / name;
    write_file_if_stale(target, wasm);
    std::printf("WASMHEX_TYPED_REF_UNSUPPORTED_GC_GEN %s bytes=%zu\n", name, wasm.size());
  }
  return 0;
}

int run_gen_fixtures(int argc, char **argv)
{
  if ( argc < 3 )
  {
    std::fprintf(stderr,
                 "usage: %s <component-multi|validator-states|pclntab-names|"
                 "brtable-wat|differential|simd-lane-bounds|typed-ref-unsupported-gc> "
                 "<dir> [<dir2>]\n",
                 argv[0]);
    return 2;
  }
  const std::string command = argv[1];
  const std::filesystem::path dir1 = argv[2];
  if ( command == "component-multi" )
    return gen_component_multi(dir1);
  if ( command == "validator-states" )
    return gen_validator_states(dir1);
  if ( command == "pclntab-names" )
    return gen_pclntab_names(dir1);
  if ( command == "brtable-wat" )
    return gen_brtable_wat(dir1);
  if ( command == "differential" && argc >= 4 )
    return gen_differential(dir1, argv[3]);
  if ( command == "simd-lane-bounds" )
    return gen_simd_lane_bounds(dir1);
  if ( command == "typed-ref-unsupported-gc" )
    return gen_typed_ref_unsupported_gc(dir1);
  std::fprintf(stderr, "gen_fixtures: unknown or unported subcommand '%s'\n", argv[1]);
  return 2;
}

} // namespace

int main(int argc, char **argv)
{
  try
  {
    return run_gen_fixtures(argc, argv);
  }
  catch ( const std::exception &ex )
  {
    std::fprintf(stderr, "gen_fixtures error: %s\n", ex.what());
    return 1;
  }
}
