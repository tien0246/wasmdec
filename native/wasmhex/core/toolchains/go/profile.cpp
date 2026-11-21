// Go toolchain version profile — implementation. See profile.hpp.
#include "profile.hpp"

#include <cstdio>
#include <cstring>

namespace wasmhex::loader
{

namespace
{
// Major/minor -> family table. Everything at or above 1.28
// lands in unknown until a corpus proves the shape — no guessing.
go_profile_t family_for(int major, int minor)
{
  if ( major != 1 )
    return go_profile_t::unknown;
  if ( minor <= 20 )
    return go_profile_t::go1_19_legacy;
  if ( minor <= 25 )
    return go_profile_t::go1_23_series;
  if ( minor == 26 )
    return go_profile_t::go1_26_series;
  if ( minor == 27 )
    return go_profile_t::go1_27_series;
  return go_profile_t::unknown;
}

void fill_expectations(go_profile_info_t &info)
{
  switch ( info.profile )
  {
    case go_profile_t::go1_19_legacy:
      info.v_registers = false;
      info.wasmexport_wrapper = false;
      info.pc_f_shift = 0;
      info.wasm_ptr_is_load = false;
      info.max_blocks_per_function = 0; // no master cap
      break;
    case go_profile_t::go1_23_series:
      info.v_registers = false;
      info.wasmexport_wrapper = false;
      info.pc_f_shift = 0;
      info.wasm_ptr_is_load = false;
      info.max_blocks_per_function = 0;
      break;
    case go_profile_t::go1_26_series:
      info.v_registers = true;
      info.wasmexport_wrapper = false;
      info.pc_f_shift = 16;
      info.wasm_ptr_is_load = false;
      info.max_blocks_per_function = 65536;
      break;
    case go_profile_t::go1_27_series:
      info.v_registers = true;
      info.wasmexport_wrapper = true;
      info.pc_f_shift = 16;
      info.wasm_ptr_is_load = true;
      info.max_blocks_per_function = 65536;
      break;
    default: break;
  }
}
} // namespace

void go_profile_info_t::apply_expectations()
{
  fill_expectations(*this);
}

bool parse_go_version_string(const std::string &text, int *major, int *minor)
{
  // Accepts "go1.26.5", "go1.27", "go1.19.3"; rejects everything else.
  if ( text.size() < 5 || text.compare(0, 4, "go1.") != 0 )
    return false;
  size_t pos = 4;
  int values[2] = { 0, 0 };
  int filled = 0;
  while ( filled < 2 )
  {
    if ( pos >= text.size() || text[pos] < '0' || text[pos] > '9' )
      return false;
    int value = 0;
    while ( pos < text.size() && text[pos] >= '0' && text[pos] <= '9' )
    {
      value = value * 10 + (text[pos] - '0');
      if ( value > 999 )
        return false;
      ++pos;
    }
    values[filled++] = value;
    if ( pos < text.size() && text[pos] == '.' )
      ++pos;
    else
      break;
  }
  if ( filled == 0 )
    return false;
  if ( major != nullptr )
    *major = 1;
  if ( minor != nullptr )
    *minor = values[0];
  return true;
}

go_profile_t go_profile_for_version(int major, int minor)
{
  return family_for(major, minor);
}

const char *go_profile_info_t::name() const
{
  switch ( profile )
  {
    case go_profile_t::none: return "none";
    case go_profile_t::go1_19_legacy: return "go1.19-legacy";
    case go_profile_t::go1_23_series: return "go1.23-series";
    case go_profile_t::go1_26_series: return "go1.26-series";
    case go_profile_t::go1_27_series: return "go1.27-series";
    case go_profile_t::unknown: return "unknown";
  }
  return "?";
}

} // namespace wasmhex::loader
