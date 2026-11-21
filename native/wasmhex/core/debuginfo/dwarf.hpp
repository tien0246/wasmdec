// The pure DWARF decoder of core/debuginfo. The IDA-facing
// entry point stays in loader/dwarf.cpp: external_debug_info file IO uses the
// qf* SDK wrappers and every diagnostic goes to msg() (the IDA log channel),
// so only the byte-level DIE/section decoding is SDK-free and moves here.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../types.hpp"

namespace wasmhex::loader
{

// Section reference triple used by strx/addrx form resolution.
struct dwarf_sections_ref_t
{
  const dwarf_section_ref_t *str = nullptr;
  const dwarf_section_ref_t *str_offsets = nullptr;
  const dwarf_section_ref_t *addr = nullptr;
};

struct dwarf_local_entry_t
{
  std::string name;
  bool has_name = false;
  bool is_parameter = false;
  int64 wasm_local = -1;      // DW_OP_WASM_location kind 0 index, -1 unknown
  uint64 origin = UINT64_MAX; // CU-relative DIE with the real name
};

struct dwarf_subprogram_t
{
  uint64 die = UINT64_MAX; // CU-relative DIE offset
  std::string name;
  bool has_name = false;
  bool has_low_pc = false;
  uint64 low_pc = 0;
  bool declaration_only = false;
  uint64 origin = UINT64_MAX;
  std::vector<dwarf_local_entry_t> entries;
};

struct dwarf_data_variable_t
{
  std::string name;
  uint64 address = 0;
};

struct dwarf_results_t
{
  std::vector<dwarf_subprogram_t> subprograms;
  std::vector<dwarf_data_variable_t> data_variables;
  size_t skipped_units = 0;
};

// One pass over the .debug_info/.debug_abbrev compile units. Reports a
// DWARF64 marker through *saw_dwarf64 (once) instead of logging; the caller
// owns the log channel.
bool parse_dwarf_units(const dwarf_section_ref_t &info, const dwarf_section_ref_t &abbrev,
                       const dwarf_sections_ref_t &sections, dwarf_results_t *results,
                       bool *saw_dwarf64);

// Uleb128 section-size helper shared with the loader-side external-file scan.
bool section_uleb_size(const uchar *data, size_t size, size_t *pos, uint64 *value);

} // namespace wasmhex::loader
