// Go toolchain version profile. Three detection layers AND together
// (import namespace, producers processed-by version, pclntab wire
// format); a MISMATCH is reported as `unknown` with a diagnostic, never
// guessed.
#pragma once

#include <cstdint>
#include <string>

namespace wasmhex::loader
{

enum class go_profile_t : std::uint8_t
{
  none = 0,      // not a Go module
  go1_19_legacy, // <=1.20: import namespace "go", pclntab F0/FA wire
  go1_23_series, // 1.21-1.25: "gojs", pre-V-register codegen
  go1_26_series, // 1.26.x: PC_B dispatch + V0-V15 registers
  go1_27_series, // 1.27.x: wasmexport wrapper, PC_F bits 16..47
  unknown,       // conflicting evidence — refuses to guess
};

struct go_profile_info_t
{
  go_profile_t profile = go_profile_t::none;

  // Raw evidence from the three layers (kept for diagnostics; empty when
  // the layer did not run or found nothing).
  std::string import_namespace;  // "gojs" / "go" / ""
  std::string producers_version; // "go1.26.5" from processed-by
  std::string pclntab_wire;      // "F1" / "F0" / "FA" / ""

  // Version-differentiated constants; each entry records what
  // the profile EXPECTS so verify() can contradict it with the module.
  bool v_registers = false;             // V0-V15 v128 temporaries (>=1.25)
  bool wasmexport_wrapper = false;      // runtime.notInitialized SP==0 check (>=1.24)
  int pc_f_shift = 0;                   // PC_F bits: 16..47 master (0 = legacy path)
  bool wasm_ptr_is_load = false;        // WasmPtr import via i32.load (master)
  uint32_t max_blocks_per_function = 0; // 65,536 on master

  // Human-readable reason when profile == unknown (stable prefix
  // "go profile mismatch:" — callers surface it verbatim).
  std::string mismatch_reason;

  bool resolved_go() const
  {
    return profile != go_profile_t::none && profile != go_profile_t::unknown;
  }
  const char *name() const;

  // Fill the version-differentiated constants for the resolved profile
  // (the expectations table) — the verify() pass compares these
  // against the module and may demote the profile to unknown.
  void apply_expectations();
  bool v_registers_expects() const { return v_registers; }
};

// Classify a "go1.NN[.patch]" producers string; returns false when the
// string is not a Go version at all.
bool parse_go_version_string(const std::string &text, int *major, int *minor);

// Map a version (major.minor) to the profile family.
go_profile_t go_profile_for_version(int major, int minor);

} // namespace wasmhex::loader
