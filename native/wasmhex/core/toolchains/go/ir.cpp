// Go function IR built once at module load time — implementation.
// See ir.hpp.
#include "ir.hpp"

#include "types.hpp"

#include "../../binary/instruction_stream.hpp"
#include "../../binary/instructions.hpp"
#include "../../types.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace
{
#define WASMHEX_GEN_GO_IR_BYTE(name, byte, sub, imm, mnem, flags)                                  \
  [[maybe_unused]] constexpr std::uint8_t k_ir_##name = std::uint8_t(byte);
WASMHEX_ITYPES(WASMHEX_GEN_GO_IR_BYTE)
#undef WASMHEX_GEN_GO_IR_BYTE
} // namespace

namespace wasmhex::loader
{

namespace
{
// Build marker verified in both deployed dylibs (procs + loaders) via
// strings before each test run.
extern "C" __attribute__((used)) const char kWasmhexM3dG2BuildMarker[]
  = "WASMHEX_BUILD_MARKER_G6_PANIC_NORETURN_COLD_TAIL";
extern "C" __attribute__((used)) const char kWasmhexG5FrameBuildMarker[]
  = "WASMHEX_BUILD_MARKER_G5_FRAME_EVIDENCE";

constexpr std::uint32_t kGoIrTailMagic = 0x474F4952u; // "GOIR"
constexpr std::uint32_t kGoIrTailVersion = 3u;
constexpr std::uint32_t kGoIrPcfTailMagic = 0x474F5043u; // "GOPC"
constexpr std::uint32_t kGoIrPcfTailVersion = 1u;
constexpr std::uint32_t kGoIrLoopTailMagic = 0x474F4C50u; // "GOLP"
constexpr std::uint32_t kGoIrLoopTailVersion = 1u;
constexpr std::uint32_t kGoIrFrameTailMagic = 0x474F4652u;    // "GOFR"
constexpr std::uint32_t kGoIrFrameTailVersion = 2u;           // v2: +caller-proof verdict
constexpr std::uint32_t kGoIrPrologueTailMagic = 0x474F4D53u; // "GOMS"
constexpr std::uint32_t kGoIrPrologueTailVersion = 1u;
constexpr std::uint32_t kGoIrWbTailMagic = 0x474F5742u; // "GOWB"
constexpr std::uint32_t kGoIrWbTailVersion = 1u;
constexpr std::uint32_t kGoFunctionIrResultsStateMax = 4u;
constexpr std::size_t kGoWbMaxSites = 256u; // per-body proven WB sites cap

// Recognizer caps (fail-closed bounds; anything outside leaves frame_size
// at UINT32_MAX or poisons the evidence entirely).
constexpr std::uint32_t kGoFrameWindowCap = 256u;   // incoming-window scan span
constexpr std::uint32_t kGoFrameResultSpan = 64u;   // result slots past param window
constexpr std::size_t kGoFrameMaxResults = 256u;    // per-body result records
constexpr std::size_t kGoFrameValueInsns = 2u;      // value push between base and store
constexpr std::size_t kGoFrameEpilogueWindow = 10u; // insns store -> frame restore

bool profile_builds_go_ir(go_profile_t profile)
{
  return profile == go_profile_t::go1_23_series || profile == go_profile_t::go1_26_series
    || profile == go_profile_t::go1_27_series;
}

// Recover the Go frame size (bytes subtracted from SP on entry) and the
// callee's incoming-argument window size (upper bound on caller outgoing-arg
// offset relative to SP before the call's SP-=8).
//
// In Go wasm (cmd/internal/obj/wasm), a callee accesses its incoming
// parameters at `[SP + framesize + 8 + off]`. Across a call, the caller writes
// outgoing arguments at `[SP + off]`:
//   - Either via `local.get 1; i64.extend_i32_u; i64.const off; i64.add;
//     i32.wrap_i64; <val>; store memarg_offset=0` (standard Go compiler path:
//     memarg_offset is 0),
//   - Or via `local.get 1; <val>; store memarg_offset=off` when `off` falls
//     inside the callee's outgoing-argument window `[0, arg_window_bytes)`.
// Any store before the call whose `memarg_offset > 0` falls at or above the
// callee's outgoing-argument window is a caller local-frame spill (e.g.
// `SP + 136`, `SP + 168`) and must NOT be collected as a call argument.
std::uint32_t compute_callee_arg_window(const module_t &module, std::size_t body_index)
{
  if ( body_index >= module.bodies.size() )
    return 0;
  const body_t &body = module.bodies[body_index];
  if ( body.offset >= module.code.size() || body.size == 0 )
    return 0;
  const uchar *p = module.code.data() + body.offset;
  const std::size_t avail
    = std::min(std::size_t(body.size), module.code.size() - std::size_t(body.offset));
  binary::insn_cursor_t cursor{ p, avail, 0, nullptr };
  std::vector<binary::decoded_insn_t> insns;
  insns.reserve(avail / 2);
  binary::decoded_insn_t decoded;
  while ( cursor.pos < avail && binary::next_insn(cursor, &decoded) )
    insns.push_back(decoded);

  std::uint32_t frame_size = 0;
  if ( insns.size() >= 4 && insns[0].opcode == 0x23 && insns[0].imm[0] == 0
       && insns[1].opcode == 0x41 && insns[2].opcode == 0x6B && insns[3].opcode == 0x24
       && insns[3].imm[0] == 0 )
  {
    frame_size = std::uint32_t(insns[1].imm[0]);
  }
  else
  {
    for ( std::size_t k = 0; k + 7 < insns.size(); ++k )
    {
      if ( insns[k].opcode == 0x20 && insns[k].imm[0] == 1 && insns[k + 1].opcode == 0x41
           && insns[k + 2].opcode == 0x6B && insns[k + 3].opcode == 0x22 && insns[k + 3].imm[0] == 1
           && insns[k + 4].opcode == 0x24 && insns[k + 4].imm[0] == 0 )
      {
        // Skip call-site SP-=8 anchors (`local.get 1; i64.const ret; i64.store 0`).
        if ( insns[k + 5].opcode == 0x20 && insns[k + 5].imm[0] == 1 && insns[k + 6].opcode == 0x42
             && insns[k + 7].opcode == 0x37 && insns[k + 7].imm[1] == 0 )
          continue;
        frame_size = std::uint32_t(insns[1 + k].imm[0]);
        break;
      }
    }
  }

  const std::uint64_t param_base = std::uint64_t(frame_size) + 8u;
  std::uint32_t max_arg_end = 0;
  for ( std::size_t k = 1; k < insns.size(); ++k )
  {
    const uchar op = insns[k].opcode;
    if ( op < 0x28 || op > 0x35 )
      continue; // scalar loads only
    const std::uint32_t width = (op == 0x29 || op == 0x2B)     ? 8u
      : (op == 0x28 || op == 0x2A || op == 0x34 || op == 0x35) ? 4u
      : (op == 0x2E || op == 0x2F || op == 0x32 || op == 0x33) ? 2u
                                                               : 1u;
    std::uint64_t off = UINT64_MAX;
    if ( insns[k - 1].opcode == 0x20 && insns[k - 1].imm[0] == 1 )
    {
      off = insns[k].imm[1];
    }
    else if ( k >= 5 && insns[k - 5].opcode == 0x20 && insns[k - 5].imm[0] == 1
              && insns[k - 4].opcode == 0xAD && insns[k - 3].opcode == 0x42
              && insns[k - 2].opcode == 0x7C && insns[k - 1].opcode == 0xA7 )
    {
      off = insns[k - 3].imm[0] + insns[k].imm[1];
    }
    if ( off != UINT64_MAX && off >= param_base && off < param_base + 256u )
    {
      const std::uint32_t end = std::uint32_t(off - param_base) + width;
      if ( end > max_arg_end )
        max_arg_end = end;
    }
  }
  return max_arg_end;
}

bool is_morestack_function_name(const std::string &name)
{
  return name == "runtime.morestack" || name == "runtime.morestack_noctxt";
}

// Scalar memory-op width by opcode (loads 0x28..0x35, stores 0x36..0x3E).
std::uint32_t go_mem_op_width(uchar opcode)
{
  switch ( opcode )
  {
    case 0x29: // i64.load
    case 0x2B: // f64.load
    case 0x37: // i64.store
    case 0x39: // f64.store
      return 8u;
    case 0x28: // i32.load
    case 0x2A: // f32.load
    case 0x34: // i64.load32_s
    case 0x35: // i64.load32_u
    case 0x36: // i32.store
    case 0x38: // f32.store
    case 0x3E: // i64.store32
      return 4u;
    case 0x2E: // i32.load16_s
    case 0x2F: // i32.load16_u
    case 0x32: // i64.load16_s
    case 0x33: // i64.load16_u
    case 0x3B: // i32.store16
    case 0x3D: // i64.store16
      return 2u;
    case 0x2C: // i32.load8_s
    case 0x2D: // i32.load8_u
    case 0x30: // i64.load8_s
    case 0x31: // i64.load8_u
    case 0x3A: // i32.store8
    case 0x3C: // i64.store8
      return 1u;
    default: return 0u;
  }
}

// Resolve the SP-relative address of the scalar memory op at index `k`
// when its base is the cached frame SP in local 1. Two Go compiler forms:
//   - memarg:      `local.get 1 [<value>] load/store off` (off = memarg)
//   - address add: `local.get 1; i64.extend_i32_u; i64.const c; i64.add;
//                   i32.wrap_i64 [<value>] load/store 0` (addr = c + memarg)
// The optional value push between base and store is at most
// `kGoFrameValueInsns` non-control instructions. Returns false when the base
// is not provably `local.get 1`.
bool resolve_go_sp_mem_address(const std::vector<binary::decoded_insn_t> &insns, std::size_t k,
                               std::uint64_t *addr)
{
  if ( addr == nullptr || k == 0 || kGoFrameValueInsns == 0 )
    return false;
  auto is_control = [](const binary::decoded_insn_t &insn)
  {
    return insn.opcode >= 0x02 && insn.opcode <= 0x11;
  };
  // A value push between base and store may contain at most
  // `kGoFrameValueInsns` units, where a unit is a plain instruction or a
  // frame-relative load pair (`local.get 1; load 0x28..0x35`). A bare
  // `local.get 1` (not consumed by a load) means we walked past the value
  // into an unrelated access — reject that base candidate.
  auto value_push_clean = [&](std::size_t first, std::size_t last)
  {
    std::size_t units = 0;
    std::size_t j = first;
    while ( j < last )
    {
      if ( units >= kGoFrameValueInsns )
        return false;
      if ( insns[j].opcode == 0x20 && insns[j].imm[0] == 1 )
      {
        if ( j + 1 >= last || insns[j + 1].opcode < 0x28 || insns[j + 1].opcode > 0x35 )
          return false;
        j += 2;
      }
      else if ( is_control(insns[j]) )
      {
        return false;
      }
      else
      {
        ++j;
      }
      ++units;
    }
    return true;
  };
  // Form B first (more specific): `local.get 1; i64.extend_i32_u; i64.const c;
  // i64.add; i32.wrap_i64 [<value>] store 0` — the address-add form. Tried
  // before the memarg form so a value-side `local.get 1; load` pair never
  // masquerades as the store base (its memarg would alias the address).
  for ( std::size_t value_len = 0; value_len <= kGoFrameValueInsns; ++value_len )
  {
    if ( 5 + value_len >= k + 1 )
      break;
    const std::size_t base = k - value_len - 5;
    if ( insns[base].opcode != 0x20 || insns[base].imm[0] != 1 || insns[base + 1].opcode != 0xAD
         || insns[base + 2].opcode != 0x42 || insns[base + 3].opcode != 0x7C
         || insns[base + 4].opcode != 0xA7 || insns[k].imm[1] != 0 )
    {
      continue;
    }
    if ( !value_push_clean(base + 5, k) )
      continue;
    *addr = insns[base + 2].imm[0];
    return true;
  }
  // Form A: walk back over the (possibly empty) value push to `local.get 1`;
  // address = memarg. Loads carry no value push (base directly before).
  for ( std::size_t value_len = 0; value_len <= kGoFrameValueInsns; ++value_len )
  {
    if ( value_len >= k )
      break;
    const std::size_t base = k - value_len - 1;
    if ( insns[base].opcode != 0x20 || insns[base].imm[0] != 1 )
      continue;
    if ( !value_push_clean(base + 1, k) )
      continue;
    *addr = insns[k].imm[1];
    return true;
  }
  return false;
}

// Collect the frame evidence for one Go body — frame size from the
// case-0 prologue (`local.get 1; i32.const K; i32.sub; local.tee 1;
// global.set 0`, call-site `SP -= 8` anchors excluded, mirroring
// compute_callee_arg_window), the incoming-param lower bound from scalar
// loads in [K+8, K+8+kGoFrameWindowCap), and result-slot stores at/after the
// param window that feed a frame epilogue. Fail-closed: any store into the
// param window (param write-back) or more than kGoFrameMaxResults records
// poisons the whole evidence (frame_size stays UINT32_MAX).
void collect_go_frame_evidence(const std::vector<binary::decoded_insn_t> &insns,
                               go_function_ir_t &ir)
{
  // 1. Frame size from the prologue (first non-anchor `SP -= K` on local 1).
  std::uint32_t frame_size = UINT32_MAX;
  if ( insns.size() >= 4 && insns[0].opcode == 0x23 && insns[0].imm[0] == 0
       && insns[1].opcode == 0x41 && insns[2].opcode == 0x6B && insns[3].opcode == 0x24
       && insns[3].imm[0] == 0 )
  {
    frame_size = std::uint32_t(insns[1].imm[0]);
  }
  else
  {
    for ( std::size_t k = 0; k + 7 < insns.size(); ++k )
    {
      if ( insns[k].opcode == 0x20 && insns[k].imm[0] == 1 && insns[k + 1].opcode == 0x41
           && insns[k + 2].opcode == 0x6B && insns[k + 3].opcode == 0x22 && insns[k + 3].imm[0] == 1
           && insns[k + 4].opcode == 0x24 && insns[k + 4].imm[0] == 0 )
      {
        // Skip call-site SP-=8 anchors (`local.get 1; i64.const ret; i64.store 0`).
        if ( insns[k + 5].opcode == 0x20 && insns[k + 5].imm[0] == 1 && insns[k + 6].opcode == 0x42
             && insns[k + 7].opcode == 0x37 && insns[k + 7].imm[1] == 0 )
          continue;
        frame_size = std::uint32_t(insns[1 + k].imm[0]);
        break;
      }
    }
  }
  if ( frame_size == UINT32_MAX || frame_size == 0 )
    return; // no proven nonzero frame: no evidence (zero-frame bodies would
            // alias outgoing-arg stores into the incoming window)

  const std::uint64_t param_base = std::uint64_t(frame_size) + 8u;

  // 2. Param window: max end of scalar loads with SP base in the window.
  std::uint32_t param_window = 0;
  for ( std::size_t k = 1; k < insns.size(); ++k )
  {
    if ( go_mem_op_width(insns[k].opcode) == 0 || insns[k].opcode > 0x35 )
      continue; // scalar loads only
    std::uint64_t addr = 0;
    if ( !resolve_go_sp_mem_address(insns, k, &addr) )
      continue;
    if ( addr >= param_base && addr < param_base + kGoFrameWindowCap )
    {
      const std::uint32_t end = std::uint32_t(addr - param_base) + go_mem_op_width(insns[k].opcode);
      if ( end > param_window )
        param_window = end;
    }
  }
  if ( param_window >= kGoFrameWindowCap )
    return; // window spills past the scan span: fail-closed, no evidence

  // 3. Result-slot stores: at/after the param window, feeding an epilogue.
  std::vector<go_result_ir_t> results;
  for ( std::size_t k = 1; k < insns.size(); ++k )
  {
    const std::uint32_t width = go_mem_op_width(insns[k].opcode);
    if ( width == 0 || insns[k].opcode < 0x36 )
      continue; // scalar stores only
    std::uint64_t addr = 0;
    if ( !resolve_go_sp_mem_address(insns, k, &addr) )
      continue;
    if ( addr < param_base || addr >= param_base + kGoFrameWindowCap )
      continue;
    const std::uint32_t rel = std::uint32_t(addr - param_base);
    if ( rel < param_window )
      return; // param write-back: poison the frame evidence
    if ( rel >= param_window + kGoFrameResultSpan )
      continue;
    // Epilogue adjacency: within kGoFrameEpilogueWindow instructions (no
    // control opcode in between) the frame restore
    // `local.get 1; i32.const K; i32.add; local.tee 1; global.set 0` appears.
    bool epilogue = false;
    for ( std::size_t j = k + 1; j < insns.size() && j <= k + kGoFrameEpilogueWindow; ++j )
    {
      const uchar op = insns[j].opcode;
      if ( op >= 0x02 && op <= 0x11 )
        break; // control boundary before the restore
      if ( op == 0x41 && std::uint32_t(insns[j].imm[0]) == frame_size && j + 3 < insns.size()
           && insns[j + 1].opcode == 0x6A && insns[j + 2].opcode == 0x22 && insns[j + 2].imm[0] == 1
           && insns[j + 3].opcode == 0x24 && insns[j + 3].imm[0] == 0 && insns[j - 1].opcode == 0x20
           && insns[j - 1].imm[0] == 1 )
      {
        epilogue = true;
        break;
      }
    }
    if ( !epilogue )
      continue;
    go_result_ir_t result;
    result.opcode = insns[k].opcode;
    result.sp_offset = rel;
    result.store_offset = insns[k].offset;
    results.push_back(result);
    if ( results.size() > kGoFrameMaxResults )
      return; // pathological: poison the frame evidence
  }

  ir.frame_size = frame_size;
  ir.param_window = param_window;
  ir.results = std::move(results);
}

// (decode_go_body_insns lives below, outside the anonymous namespace —
// declared in ir.hpp for the call-argument and receiver walks.)

void cross_check_go_result_callers(module_t &module)
{
  const std::size_t imports = module.imported_func_types.size();

  struct result_evidence_t
  {
    bool eligible = false;  // frame_valid + non-ambiguous result records
    bool ambiguous = false; // overlapping deduplicated slots
    bool has_caller = false;
    bool mismatch = false;
    std::uint32_t max_window = 0;
    std::uint32_t min_offset = UINT32_MAX;                       // lowest result slot
    std::uint32_t extent = 0;                                    // one past the highest result byte
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges; // [begin, end)
  };
  std::vector<result_evidence_t> evidence(module.bodies.size());
  for ( std::size_t i = 0; i < module.bodies.size(); ++i )
  {
    const body_t &body = module.bodies[i];
    const go_function_ir_t &ir = body.go_ir;
    if ( !ir.present || !ir.frame_valid(body.size) || ir.results.empty() )
      continue;
    result_evidence_t &ev = evidence[i];
    for ( const go_result_ir_t &result : ir.results )
    {
      const std::uint32_t width = go_mem_op_width(uchar(result.opcode));
      ev.ranges.emplace_back(result.sp_offset, result.sp_offset + width);
      if ( result.sp_offset < ev.min_offset )
        ev.min_offset = result.sp_offset;
      if ( result.sp_offset + width > ev.extent )
        ev.extent = result.sp_offset + width;
    }
    std::sort(ev.ranges.begin(), ev.ranges.end());
    ev.ranges.erase(std::unique(ev.ranges.begin(), ev.ranges.end()), ev.ranges.end());
    for ( std::size_t k = 1; k < ev.ranges.size(); ++k )
    {
      if ( ev.ranges[k].first < ev.ranges[k - 1].second )
      {
        ev.ambiguous = true; // overlapping distinct slots: refuse
        break;
      }
    }
    ev.eligible = !ev.ambiguous;
  }

  for ( std::size_t b = 0; b < module.bodies.size(); ++b )
  {
    const go_function_ir_t &caller = module.bodies[b].go_ir;
    if ( !caller.present || caller.calls.empty() )
      continue;
    std::vector<binary::decoded_insn_t> insns;
    bool insns_ready = false;
    auto ensure_insns = [&]()
    {
      if ( !insns_ready )
      {
        insns_ready = decode_go_body_insns(module, b, &insns);
      }
      return insns_ready;
    };
    // Index of the first decoded instruction at/after an in-body offset.
    auto index_at = [&insns](std::uint32_t offset)
    {
      return std::size_t(std::lower_bound(insns.begin(), insns.end(), offset,
                                          [](const binary::decoded_insn_t &insn, std::uint32_t off)
                                          { return insn.offset < off; })
                         - insns.begin());
    };
    // Earliest recorded offset of a call record (its SP-decrement leg, first
    // argument store, or the return-address push).
    auto call_start = [](const go_call_ir_t &call) -> std::uint32_t
    {
      std::uint32_t begin = call.store_offset;
      if ( call.sp_arith_offsets[0] != UINT32_MAX && call.sp_arith_offsets[0] < begin )
        begin = call.sp_arith_offsets[0];
      for ( const go_arg_ir_t &arg : call.args )
        if ( arg.store_offset < begin )
          begin = arg.store_offset;
      return begin;
    };
    for ( std::size_t c = 0; c < caller.calls.size(); ++c )
    {
      const go_call_ir_t &call = caller.calls[c];
      if ( call.fn_index == UINT32_MAX || call.fn_index < imports )
        continue;
      const std::size_t callee = std::size_t(call.fn_index - imports);
      if ( callee >= module.bodies.size() || !evidence[callee].eligible )
        continue;
      result_evidence_t &ev = evidence[callee];
      ev.has_caller = true;
      if ( ev.mismatch )
        continue;
      std::uint32_t window = 0;
      for ( const go_arg_ir_t &arg : call.args )
      {
        const std::uint32_t width = go_mem_op_width(uchar(arg.opcode));
        if ( width == 0 )
          continue; // unknown arg width: this store never passed the gate
        if ( arg.sp_offset + width > window )
          window = arg.sp_offset + width;
      }
      if ( window > ev.max_window )
        ev.max_window = window;
      if ( ev.min_offset < ev.max_window )
      {
        ev.mismatch = true; // "result" inside the caller-pushed args
        continue;
      }
      if ( !ensure_insns() )
      {
        ev.mismatch = true; // cannot verify the reads: fail closed
        continue;
      }
      const std::size_t scan_end
        = c + 1 < caller.calls.size() ? index_at(call_start(caller.calls[c + 1])) : insns.size();
      for ( std::size_t k = index_at(call.sp_restore_offset); k < insns.size() && k < scan_end;
            ++k )
      {
        if ( insns[k].opcode < 0x28 || insns[k].opcode > 0x35 )
          continue; // scalar loads only
        std::uint64_t addr = 0;
        if ( !resolve_go_sp_mem_address(insns, k, &addr) )
          continue;
        if ( addr < ev.min_offset || addr >= ev.extent )
          continue; // below: caller re-reading its args; above: caller frame
        const std::uint32_t width = go_mem_op_width(insns[k].opcode);
        bool covered = false;
        for ( const auto &range : ev.ranges )
          if ( addr >= range.first && addr + width <= range.second )
          {
            covered = true;
            break;
          }
        if ( !covered )
        {
          ev.mismatch = true; // reads outside every recorded slot
          break;
        }
      }
    }
  }

  for ( std::size_t i = 0; i < module.bodies.size(); ++i )
  {
    const result_evidence_t &ev = evidence[i];
    if ( !ev.eligible && ev.ambiguous )
    {
      module.bodies[i].go_ir.results_caller_state = go_function_ir_t::kResultsAmbiguous;
    }
    else if ( ev.eligible && ev.has_caller && !ev.mismatch )
    {
      module.bodies[i].go_ir.results_caller_state = go_function_ir_t::kResultsProven;
    }
    else if ( ev.eligible && !ev.has_caller )
    {
      module.bodies[i].go_ir.results_caller_state = go_function_ir_t::kResultsNoCaller;
    }
    else if ( ev.eligible )
    {
      module.bodies[i].go_ir.results_caller_state = go_function_ir_t::kResultsMismatch;
    }
  }
}

// Read a little-endian 64-bit value from an initialized active data segment
// of `module` when `[linear_addr, linear_addr + 8)` lies entirely inside the
// segment payload.
bool read_active_data_u64(const module_t &module, std::uint64_t linear_addr, std::uint64_t *out)
{
  for ( std::size_t i = 0; i < module.data_segment_offsets.size()
        && i < module.data_segment_sizes.size() && i < module.data_segment_data.size();
        ++i )
  {
    const std::int64_t seg_off = module.data_segment_offsets[i];
    if ( seg_off < 0 )
      continue;
    if ( i < module.data_segment_globals.size() && module.data_segment_globals[i] != UINT32_MAX )
      continue;
    const std::uint64_t base = std::uint64_t(seg_off);
    const std::uint64_t sz = module.data_segment_sizes[i];
    if ( sz < 8 || linear_addr < base || linear_addr > base + sz - 8 )
      continue;
    const std::size_t rel = std::size_t(linear_addr - base);
    if ( rel + 8 > module.data_segment_data[i].size() )
      continue;
    const uchar *bytes = module.data_segment_data[i].data() + rel;
    std::uint64_t val = 0;
    for ( unsigned b = 0; b < 8; ++b )
      val |= std::uint64_t(bytes[b]) << (b * 8);
    *out = val;
    return true;
  }
  return false;
}

// Resolve a Go table0 slot index (PC_F) to a module function index when
// table 0 is statically known and slot `slot_index` is initialized.
bool resolve_go_table0_slot(const module_t &module, std::uint32_t slot_index,
                            std::uint32_t *fn_index)
{
  for ( const table_slot_t &slot : module.table_slots )
  {
    if ( slot.table_index == 0 && slot.slot_index == slot_index )
    {
      *fn_index = slot.function_index;
      return true;
    }
  }
  return false;
}

// Check whether a sequence ending at `end_idx` (exclusive) pushes a provably
// constant 64-bit Go funcval (either `i64.const V` or `i64.const addr;
// i32.wrap_i64; i64.load align=3 off` from an initialized data segment with no
// in-module stores to `addr + off`, or a `local.get L` written in the same
// basic block by one of those forms) and resolve `V >> 16` in table 0.
bool try_resolve_constant_pc_f_callee(const module_t &module,
                                      const std::vector<binary::decoded_insn_t> &insns,
                                      std::size_t target_end_idx, std::size_t *target_begin_idx,
                                      std::uint32_t *resolved_fn)
{
  auto eval_funcval_expr
    = [&](std::size_t expr_end, std::size_t *expr_begin, std::uint64_t *val) -> bool
  {
    if ( expr_end >= 1 && insns[expr_end - 1].opcode == 0x42 )
    {
      if ( expr_begin != nullptr )
        *expr_begin = expr_end - 1;
      *val = insns[expr_end - 1].imm[0];
      return true;
    }
    if ( expr_end >= 3 && insns[expr_end - 3].opcode == 0x42 && insns[expr_end - 2].opcode == 0xA7
         && insns[expr_end - 1].opcode == 0x29 && insns[expr_end - 1].imm[0] == 3 )
    {
      const std::uint64_t addr
        = std::uint64_t(std::uint32_t(insns[expr_end - 3].imm[0])) + insns[expr_end - 1].imm[1];
      if ( expr_begin != nullptr )
        *expr_begin = expr_end - 3;
      return read_active_data_u64(module, addr, val);
    }
    return false;
  };

  std::uint64_t funcval = 0;
  std::size_t begin_idx = target_end_idx;
  bool have_funcval = false;

  if ( target_end_idx >= 1 && insns[target_end_idx - 1].opcode == 0x20 )
  {
    const std::uint64_t loc = insns[target_end_idx - 1].imm[0];
    begin_idx = target_end_idx - 1;
    // Walk backwards within the same straight-line block for the defining
    // `local.set loc` or `local.tee loc`.
    for ( std::size_t b = begin_idx; b > 0; )
    {
      --b;
      const uchar op = insns[b].opcode;
      if ( op == 0x02 || op == 0x03 || op == 0x04 || op == 0x05 || op == 0x0B || op == 0x0C
           || op == 0x0D || op == 0x0E || op == 0x0F || op == 0x10 || op == 0x11 )
      {
        break;
      }
      if ( (op == 0x21 || op == 0x22) && insns[b].imm[0] == loc )
      {
        have_funcval = eval_funcval_expr(b, nullptr, &funcval);
        break;
      }
    }
  }
  else
  {
    have_funcval = eval_funcval_expr(target_end_idx, &begin_idx, &funcval);
    if ( !have_funcval )
    {
      // Still advance `begin_idx` across a known dynamic target-push shape so
      // the caller can skip any preceding CTXT global.set when walking args.
      if ( target_end_idx >= 3 && insns[target_end_idx - 1].opcode == 0x29
           && insns[target_end_idx - 2].opcode == 0xA7
           && (insns[target_end_idx - 3].opcode == 0x20 || insns[target_end_idx - 3].opcode == 0x23
               || insns[target_end_idx - 3].opcode == 0x42) )
      {
        begin_idx = target_end_idx - 3;
      }
      else if ( target_end_idx >= 4 && insns[target_end_idx - 1].opcode == 0x29
                && insns[target_end_idx - 2].opcode == 0xA7
                && insns[target_end_idx - 3].opcode == 0x29
                && insns[target_end_idx - 4].opcode == 0x20 )
      {
        begin_idx = target_end_idx - 4;
      }
    }
  }

  if ( target_begin_idx != nullptr )
    *target_begin_idx = begin_idx;

  if ( !have_funcval || (funcval & 0xFFFFu) != 0 )
    return false;
  const std::uint64_t slot = funcval >> 16;
  if ( slot > UINT32_MAX )
    return false;
  return resolve_go_table0_slot(module, std::uint32_t(slot), resolved_fn);
}

// Proven bytecode recognizer for the 41-byte `wasm_pc_f_loop` runtime
// trampoline (`cmd/internal/obj/wasm` outer PC_F dispatch loop):
//   global.get 7 (PAUSE); i32.eqz; if (void)
//     loop (void)
//       global.get 0 (SP); i32.const 8; i32.sub; i32.load16_u align=1 off=0;
//       global.get 0 (SP); i32.const 8; i32.sub; i32.load align=2 off=2;
//       call_indirect 0 (type 0); drop;
//       global.get 7 (PAUSE); i32.eqz; br_if 0;
//     end
//   end
//   i32.const 0; global.set 7 (PAUSE); return; unreachable; end
bool match_wasm_pc_f_loop_body(const module_t &module, std::size_t body_index,
                               const std::vector<binary::decoded_insn_t> &insns,
                               std::uint32_t *out_call_offset)
{
  if ( body_index >= module.defined_func_types.size() || body_index >= module.bodies.size() )
    return false;
  const std::uint32_t type_idx = module.defined_func_types[body_index];
  if ( type_idx >= module.types.size() )
    return false;
  const wasm_type_t &sig = module.types[type_idx];
  if ( !sig.params.empty() || !sig.results.empty() )
    return false;
  const body_t &body = module.bodies[body_index];
  if ( !body.local_types.empty() || insns.size() != 24 )
    return false;
  if ( insns[0].opcode != 0x23 || insns[0].imm[0] != 7 || insns[1].opcode != 0x45
       || insns[2].opcode != 0x04 || insns[3].opcode != 0x03 || insns[4].opcode != 0x23
       || insns[4].imm[0] != 0 || insns[5].opcode != 0x41 || insns[5].imm[0] != 8
       || insns[6].opcode != 0x6B || insns[7].opcode != 0x2F || insns[7].imm[0] != 1
       || insns[7].imm[1] != 0 || insns[8].opcode != 0x23 || insns[8].imm[0] != 0
       || insns[9].opcode != 0x41 || insns[9].imm[0] != 8 || insns[10].opcode != 0x6B
       || insns[11].opcode != 0x28 || insns[11].imm[0] != 2 || insns[11].imm[1] != 2
       || insns[12].opcode != 0x11 || insns[12].imm[1] != 0 || insns[13].opcode != 0x1A
       || insns[14].opcode != 0x23 || insns[14].imm[0] != 7 || insns[15].opcode != 0x45
       || insns[16].opcode != 0x0D || insns[16].imm[0] != 0 || insns[17].opcode != 0x0B
       || insns[18].opcode != 0x0B || insns[19].opcode != 0x41 || insns[19].imm[0] != 0
       || insns[20].opcode != 0x24 || insns[20].imm[0] != 7 || insns[21].opcode != 0x0F
       || insns[22].opcode != 0x00 || insns[23].opcode != 0x0B )
  {
    return false;
  }
  const std::uint32_t call_type_idx = std::uint32_t(insns[12].imm[0]);
  if ( call_type_idx >= module.types.size() )
    return false;
  const wasm_type_t &call_sig = module.types[call_type_idx];
  if ( call_sig.params.size() != 1 || call_sig.params[0] != 0x7F || call_sig.results.size() != 1
       || call_sig.results[0] != 0x7F )
    return false;
  if ( out_call_offset != nullptr )
    *out_call_offset = insns[12].offset;
  return true;
}

bool match_morestack_noctxt_wrapper_body(const std::vector<binary::decoded_insn_t> &insns,
                                         std::uint32_t *out_morestack_fn)
{
  if ( insns.size() != 11 )
    return false;
  if ( insns[0].opcode != 0x23 || insns[0].imm[0] != 0 || insns[1].opcode != 0x21
       || insns[1].imm[0] != 1 || insns[2].opcode != 0x42 || insns[2].imm[0] != 0
       || insns[3].opcode != 0x24 || insns[3].imm[0] != 1 || insns[4].opcode != 0x41
       || insns[4].imm[0] != 0 || insns[5].opcode != 0x10 || insns[6].opcode != 0x23
       || insns[6].imm[0] != 0 || insns[7].opcode != 0x21 || insns[7].imm[0] != 1
       || insns[8].opcode != 0x0F || insns[9].opcode != 0x00 || insns[10].opcode != 0x0B )
  {
    return false;
  }
  if ( insns[5].imm[0] > UINT32_MAX )
    return false;
  if ( out_morestack_fn != nullptr )
    *out_morestack_fn = std::uint32_t(insns[5].imm[0]);
  return true;
}

bool collect_go_prologue_evidence(const std::vector<binary::decoded_insn_t> &insns,
                                  std::size_t call_idx, go_prologue_ir_t *out)
{
  if ( out == nullptr || call_idx >= insns.size() )
    return false;
  // Forward side (cheap reject first): global.get 0; local.set L; br_if; end.
  if ( call_idx + 4 >= insns.size() )
    return false;
  if ( insns[call_idx + 1].opcode != 0x23 || insns[call_idx + 1].imm[0] != 0
       || insns[call_idx + 2].opcode != 0x21 || insns[call_idx + 3].opcode != 0x0D
       || insns[call_idx + 4].opcode != 0x0B )
  {
    return false;
  }
  const std::uint64_t sp_local = insns[call_idx + 2].imm[0];

  // Backward side. `i` is the call index; every read is bounds-checked
  // before use.
  std::size_t i = call_idx;
  // [i32.const 0]? — the noctxt wrapper's CTXT=0 push.
  if ( i > 0 && insns[i - 1].opcode == 0x41 && insns[i - 1].imm[0] == 0 )
    --i;
  if ( i < 1 )
    return false;
  // i64.store align=3 off=0 — the resume-PC push.
  if ( insns[i - 1].opcode != 0x37 || insns[i - 1].imm[0] != 3 || insns[i - 1].imm[1] != 0 )
    return false;
  const std::uint32_t store_offset = insns[i - 1].offset;
  if ( i < 3 )
    return false;
  // i64.const RESUME_PC; local.get L
  if ( insns[i - 2].opcode != 0x42 || insns[i - 3].opcode != 0x20
       || insns[i - 3].imm[0] != sp_local )
  {
    return false;
  }
  if ( i < 8 )
    return false;
  // The SP -= 8 leg: global.set 0; local.tee L; i32.const 8; i32.sub;
  // local.get L. Walking back from the call: tee is 5 before, the
  // i32.sub 6 before, the i32.const 8 7 before, the leading local.get 8.
  if ( insns[i - 4].opcode != 0x24 || insns[i - 4].imm[0] != 0 || insns[i - 5].opcode != 0x22
       || insns[i - 5].imm[0] != sp_local || insns[i - 6].opcode != 0x6B
       || (insns[i - 7].opcode != 0x41 || insns[i - 7].imm[0] != 8) || insns[i - 8].opcode != 0x20
       || insns[i - 8].imm[0] != sp_local )
  {
    return false;
  }
  if ( i < 10 )
    return false;
  // `if` (void blocktype) then i32.le_u|i32.lt_u underneath.
  if ( insns[i - 9].opcode != 0x04 )
    return false;
  const std::uint32_t if_offset = insns[i - 9].offset;
  if ( insns[i - 10].opcode != 0x4D && insns[i - 10].opcode != 0x4B )
    return false;
  const std::uint32_t compare_offset = insns[i - 10].offset;
  std::size_t j = i - 10;
  // [i32.const K; i32.add]? — the large-frame variant (K > 0).
  std::uint32_t guard_const_offset = UINT32_MAX;
  std::uint32_t framesize_extra = 0;
  if ( j >= 2 && insns[j - 1].opcode == 0x6A && insns[j - 2].opcode == 0x41
       && insns[j - 2].imm[0] > 0 && insns[j - 2].imm[0] <= UINT32_MAX )
  {
    guard_const_offset = insns[j - 2].offset;
    framesize_extra = std::uint32_t(insns[j - 2].imm[0]);
    j -= 2;
  }
  // The guard load chain: i32.load a=2 off=16; i32.wrap_i64;
  // global.get 2; local.get L.
  if ( j < 4 )
    return false;
  if ( insns[j - 1].opcode != 0x28 || insns[j - 1].imm[0] != 2 || insns[j - 1].imm[1] != 16
       || insns[j - 2].opcode != 0xA7 || insns[j - 3].opcode != 0x23 || insns[j - 3].imm[0] != 2
       || insns[j - 4].opcode != 0x20 || insns[j - 4].imm[0] != sp_local )
  {
    return false;
  }
  out->sp_get_offset = insns[j - 4].offset;
  out->guard_load_offset = insns[j - 1].offset;
  out->guard_const_offset = guard_const_offset;
  out->framesize_extra = framesize_extra;
  out->compare_offset = compare_offset;
  out->if_offset = if_offset;
  out->store_offset = store_offset;
  out->call_offset = insns[call_idx].offset;
  out->br_if_offset = insns[call_idx + 3].offset;
  out->end_offset = insns[call_idx + 4].offset;
  return true;
}

} // namespace

// Decode the full instruction stream of one body (shared cursor
// walk) — outside the anonymous namespace: declared in ir.hpp since the
// Walks and the unit probes materialize from it.
bool decode_go_body_insns(const module_t &module, std::size_t body_index,
                          std::vector<binary::decoded_insn_t> *insns)
{
  const body_t &body = module.bodies[body_index];
  if ( body.offset >= module.code.size() || body.size == 0 )
    return false;
  const uchar *p = module.code.data() + body.offset;
  const std::size_t avail
    = std::min<std::size_t>(body.size, module.code.size() - std::size_t(body.offset));
  binary::insn_cursor_t cursor{ p, avail, 0, nullptr };
  binary::decoded_insn_t decoded;
  while ( cursor.pos < avail && binary::next_insn(cursor, &decoded) )
    insns->push_back(decoded);
  return !insns->empty();
}

bool is_go_terminal_noreturn_function(const go_function_ir_t &ir, bool is_morestack_or_noctxt)
{
  if ( !ir.present || ir.unwind_exit == UINT32_MAX || !ir.pc_b_register_proof || !ir.ret.empty()
       || is_morestack_or_noctxt )
    return false;
  for ( const go_jmp_ir_t &jmp : ir.jmps )
  {
    if ( std::size_t(jmp.case_index) >= ir.resume_table.targets.size() )
      return false;
    const std::uint32_t target_offset = ir.resume_table.targets[jmp.case_index];
    if ( target_offset <= jmp.br_offset )
      return false;
  }
  return true;
}

// Go write-barrier recognition at the IR
// The recognizer set moved verbatim from decompiler/rewriter_go.cpp
// (go_wb_detail): the loader now decides `runtime.gcWriteBarrier{1..8}`
// callee identity (name, or wrapper-body + core-body bytecode on stripped
// modules) and proves the whole slow-path protocol in the instruction
// stream; the ctree layer only consumes the resulting marks.

bool match_gc_write_barrier_name(const char *name, std::uint32_t *out_arity)
{
  if ( name == nullptr || out_arity == nullptr )
    return false;
  constexpr const char prefix[] = "runtime.gcWriteBarrier";
  constexpr std::size_t prefix_len = sizeof(prefix) - 1;
  if ( std::strncmp(name, prefix, prefix_len) != 0 )
    return false;
  const char digit = name[prefix_len];
  if ( digit < '1' || digit > '8' || name[prefix_len + 1] != '\0' )
    return false;
  *out_arity = std::uint32_t(digit - '0');
  return true;
}

bool match_sigpanic_name(const char *name)
{
  return name != nullptr && std::strcmp(name, "runtime.sigpanic") == 0;
}

bool match_gc_write_barrier_wrapper_body(const std::uint8_t *data, std::size_t size,
                                         std::uint32_t *out_arity, std::uint32_t *out_core_func_idx)
{
  if ( data == nullptr || size == 0 || out_arity == nullptr || out_core_func_idx == nullptr )
    return false;
  binary::insn_cursor_t cursor{ data, size, 0, nullptr };
  binary::decoded_insn_t insns[8];
  std::size_t count = 0;
  while ( cursor.pos < cursor.size )
  {
    if ( count >= sizeof(insns) / sizeof(insns[0]) )
      return false;
    if ( !binary::next_insn(cursor, &insns[count]) )
      return false;
    ++count;
  }
  if ( cursor.pos != cursor.size || count != 5 )
    return false;
  if ( insns[0].opcode != 0x42 || insns[1].opcode != 0x10 || insns[2].opcode != 0x0F
       || insns[3].opcode != 0x00 || insns[4].opcode != 0x0B )
    return false;
  const std::uint64_t bytes = insns[0].imm[0];
  if ( bytes < 8 || bytes > 64 || (bytes % 8) != 0 || insns[1].imm[0] > UINT32_MAX )
    return false;
  *out_arity = std::uint32_t(bytes / 8);
  *out_core_func_idx = std::uint32_t(insns[1].imm[0]);
  return true;
}

bool match_gc_write_barrier_core_body(const std::uint8_t *data, std::size_t size)
{
  if ( data == nullptr || size == 0 )
    return false;
  binary::insn_cursor_t cursor{ data, size, 0, nullptr };
  binary::decoded_insn_t insns[64];
  std::size_t count = 0;
  while ( cursor.pos < cursor.size )
  {
    if ( count >= sizeof(insns) / sizeof(insns[0]) )
      return false;
    if ( !binary::next_insn(cursor, &insns[count]) )
      return false;
    ++count;
  }
  if ( cursor.pos != cursor.size || count < 25 || insns[count - 1].opcode != 0x0B )
    return false;
  if ( insns[0].opcode != 0x03 || insns[1].opcode != 0x23 || insns[1].imm[0] != 2
       || insns[2].opcode != 0xA7 || insns[3].opcode != 0x29 || insns[3].imm[1] != 0x30
       || insns[4].opcode != 0x22 || insns[5].opcode != 0xA7 || insns[6].opcode != 0x29
       || insns[6].imm[1] != 0xA0 || insns[7].opcode != 0x22 || insns[8].opcode != 0xA7
       || insns[9].opcode != 0x29 || insns[9].imm[1] == 0 || insns[10].opcode != 0x22
       || insns[11].opcode != 0x20 || insns[11].imm[0] != 0 || insns[12].opcode != 0x7C
       || insns[13].opcode != 0x22 || insns[14].opcode != 0x20
       || insns[14].imm[0] != insns[7].imm[0] || insns[15].opcode != 0xA7
       || insns[16].opcode != 0x29 || insns[16].imm[1] != insns[9].imm[1] + 8
       || insns[17].opcode != 0x58 || insns[18].opcode != 0x04 )
    return false;
  return true;
}

bool go_wb_site_ir_t::wb_site_valid(std::uint32_t body_size) const
{
  if ( guard_if_offset == UINT32_MAX || call_offset == UINT32_MAX || arity < 1 || arity > 8 )
    return false;
  if ( guard_if_offset >= body_size || call_offset >= body_size || guard_if_offset >= call_offset )
    return false;
  // spill_count may be 0 (argument window written before the guard)
  if ( buf_local == UINT32_MAX )
    return false;
  return true;
}

namespace
{

bool collect_go_wb_evidence(const std::vector<binary::decoded_insn_t> &insns, std::size_t call_idx,
                            std::uint32_t arity, const std::set<std::uint32_t> &sigpanic_fns,
                            go_wb_site_ir_t *out)
{
  if ( out == nullptr || call_idx == 0 || call_idx >= insns.size() )
    return false;

  const auto is_int_store = [](std::uint32_t op)
  {
    return op == 0x36 || op == 0x37 || (op >= 0x3A && op <= 0x3E);
  };
  const auto is_scalar_load = [](std::uint32_t op)
  {
    return op == 0x28 || op == 0x29 || (op >= 0x2C && op <= 0x35);
  };

  // ---- forward side: SP refresh, buf tee, contiguous buffer writes, reloads
  std::size_t k = call_idx + 1;
  if ( k + 4 >= insns.size() )
    return false;
  if ( insns[k].opcode != 0x23 || insns[k].imm[0] != 0 || insns[k + 1].opcode != 0x21
       || insns[k + 1].imm[0] != 1 || insns[k + 2].opcode != 0x22 || insns[k + 3].opcode != 0xA7 )
  {
    return false;
  }
  const std::uint32_t buf_local = std::uint32_t(insns[k + 2].imm[0]);
  k += 4;
  // Buffer write 0 reuses the wrapped buf left on the stack by the tee pair
  // (`local.tee B; i32.wrap_i64` already pushed the address): only
  // `local.get 1; i64.load off=Sk; i64.store a=3 off=0` follows. Writes
  // 1..arity-1 reload the buf local (`local.get B; i32.wrap_i64; ...`).
  if ( k + 2 >= insns.size() || insns[k].opcode != 0x20 || insns[k].imm[0] != 1
       || insns[k + 1].opcode != 0x29 || insns[k + 1].imm[0] != 3 || insns[k + 2].opcode != 0x37
       || insns[k + 2].imm[0] != 3 || insns[k + 2].imm[1] != 0 )
  {
    return false;
  }
  k += 3;
  for ( std::uint32_t n = 1; n < arity; ++n, k += 5 )
  {
    if ( k + 4 >= insns.size() )
      return false;
    if ( insns[k].opcode != 0x20 || insns[k].imm[0] != buf_local || insns[k + 1].opcode != 0xA7
         || insns[k + 2].opcode != 0x20 || insns[k + 2].imm[0] != 1 || insns[k + 3].opcode != 0x29
         || insns[k + 3].imm[0] != 3 || insns[k + 4].opcode != 0x37 || insns[k + 4].imm[0] != 3
         || insns[k + 4].imm[1] != std::uint64_t(n) * 8u )
      return false;
  }
  // Positive-block form: the writes end the guard's then-branch (`end`
  // follows; the fast path is the fall-through). Negative form: reload units
  // `local.get 1; <scalar load off=R>; local.set/tee X` restore the live
  // register set (any count; partial byte slots reload through
  // load8_u/load16_u).
  const bool positive_block = k < insns.size() && insns[k].opcode == 0x0B;
  std::size_t reloads = 0;
  if ( !positive_block )
  {
    while ( k + 2 < insns.size() && reloads < 32 && insns[k].opcode == 0x20 && insns[k].imm[0] == 1
            && is_scalar_load(insns[k + 1].opcode)
            && (insns[k + 2].opcode == 0x21 || insns[k + 2].opcode == 0x22) )
    {
      k += 3;
      ++reloads;
    }
    if ( reloads == 0 )
      return false;
  }

  // ---- backward side: spill units (+ old-value chains), nil guard, guard
  std::size_t i = call_idx;
  std::uint32_t spills = 0;
  std::uint32_t chains = 0;
  bool old_chain = false;
  int units = 0;
  while ( units < 48 && i >= 3 )
  {
    // old-value chain: <addr source>; i32.wrap_i64; i64.load off=B;
    // local.set T (its spill, when present, is consumed by the plain-spill
    // unit that follows). The address source is a register local
    // (`local.get X`), a stack slot (`local.get 1; i64.load off=A`), or an
    // absolute constant (`i64.const K`). Multi-slot sites carry one chain
    // per written pointer field.
    if ( chains <= arity && i >= 4 && insns[i - 1].opcode == 0x21 && insns[i - 2].opcode == 0x29
         && insns[i - 3].opcode == 0xA7
         && (insns[i - 4].opcode == 0x20 || insns[i - 4].opcode == 0x42
             || (insns[i - 4].opcode == 0x23 && insns[i - 4].imm[0] <= 2)
             || (insns[i - 4].opcode == 0x29 && i >= 5 && insns[i - 5].opcode == 0x20
                 && insns[i - 5].imm[0] == 1)) )
    {
      old_chain = true;
      chains += 1;
      i -= (insns[i - 4].opcode == 0x29) ? 5 : 4;
      ++units;
      continue;
    }
    // plain spill (full or partial): local.get 1; local.get X; <int store>
    if ( is_int_store(insns[i - 1].opcode) && insns[i - 2].opcode == 0x20
         && insns[i - 3].opcode == 0x20 && insns[i - 3].imm[0] == 1 )
    {
      spills += 1;
      i -= 3;
      ++units;
      continue;
    }
    break;
  }
  (void)spills;
  (void)old_chain;

  // optional in-body nil guard: local.get X [; <scalar load off=A>]; eqz;
  // if { SP-=8; [SP]=PC; i32.const 0; call runtime.sigpanic; SP refresh;
  // br_if R } end — verified strictly backward (17 insns, or 18 when the
  // guarded pointer itself loads from the stack).
  bool nil_guard = false;
  if ( !sigpanic_fns.empty() && i >= 17 && insns[i - 1].opcode == 0x0B
       && insns[i - 2].opcode == 0x0D && insns[i - 3].opcode == 0x21 && insns[i - 3].imm[0] == 1
       && insns[i - 4].opcode == 0x23 && insns[i - 4].imm[0] == 0 && insns[i - 5].opcode == 0x10
       && insns[i - 5].imm[0] <= UINT32_MAX
       && sigpanic_fns.count(std::uint32_t(insns[i - 5].imm[0])) != 0 && insns[i - 6].opcode == 0x41
       && insns[i - 6].imm[0] == 0 && insns[i - 7].opcode == 0x37 && insns[i - 7].imm[0] == 3
       && insns[i - 7].imm[1] == 0 && insns[i - 8].opcode == 0x42 && insns[i - 9].opcode == 0x20
       && insns[i - 9].imm[0] == 1 && insns[i - 10].opcode == 0x24 && insns[i - 10].imm[0] == 0
       && insns[i - 11].opcode == 0x22 && insns[i - 11].imm[0] == 1 && insns[i - 12].opcode == 0x6B
       && insns[i - 13].opcode == 0x41 && insns[i - 13].imm[0] == 8 && insns[i - 14].opcode == 0x20
       && insns[i - 14].imm[0] == 1 && insns[i - 15].opcode == 0x04
       && (insns[i - 16].opcode == 0x45 || insns[i - 16].opcode == 0x50)
       && (insns[i - 17].opcode == 0x20                                   /* register pointer */
           || (insns[i - 17].opcode == 0x23 && insns[i - 17].imm[0] <= 2) /* ABI global (SP/g) */
           || (i >= 18 && is_scalar_load(insns[i - 17].opcode) && insns[i - 18].opcode == 0x20
               && insns[i - 18].imm[0] == 1) /* pointer loads from [SP+A] */) )
  {
    nil_guard = true;
    i -= is_scalar_load(insns[i - 17].opcode) ? 18 : 17;
  }

  // optional bookkeeping store `*(g + K) = SP` (`local.get 1; global.get 2;
  // i64.store off=K`) between the nil guard and the guard `if` — the
  // compiler records the current SP into the g struct around some barriers.
  if ( i >= 3 && insns[i - 1].opcode == 0x37 && insns[i - 2].opcode == 0x23
       && insns[i - 2].imm[0] <= 2 && insns[i - 3].opcode == 0x20 && insns[i - 3].imm[0] == 1 )
  {
    i -= 3;
  }

  // guard if — two compiler block forms:
  //   negative: <cond>; if { i32.const K; local.set 0; br D } end [end];
  //             the fast path branches away, the slow path (spills, call,
  //             writes, reloads) falls through. The optional second `end`
  //             closes the enclosing compiler block.
  //   positive: <cond>; if; spills; call; writes; end — the slow path sits
  //             inside the then-branch, the fast path is the fall-through.
  // The condition is a pure eqz/extend chain (i32.eqz 0x45, i64.eqz 0x50,
  // i64.extend_i32_u 0xAD) over a single leaf (local.get or scalar load).
  const auto cond_leaf_ok = [&](std::size_t leaf) -> bool
  {
    if ( leaf == 0 )
      return false;
    const std::uint32_t op = insns[leaf - 1].opcode;
    return op == 0x20 || is_scalar_load(op);
  };
  std::size_t g = 0;
  if ( i >= 1 && insns[i - 1].opcode == 0x04 )
  {
    // positive form: the `if` directly precedes the spill region
    g = i;
  }
  else if ( i >= 2 && insns[i - 1].opcode == 0x0B )
  {
    g = i;
    if ( g >= 2 && insns[g - 2].opcode == 0x0B )
      --g; // enclosing block end
    if ( g < 7 || insns[g - 1].opcode != 0x0B
         || (insns[g - 2].opcode != 0x0C && insns[g - 2].opcode != 0x0E)
         || insns[g - 3].opcode != 0x21 || insns[g - 3].imm[0] != 0 || insns[g - 4].opcode != 0x41
         || insns[g - 5].opcode != 0x04 )
      return false;
    g = g
      - 4; // g-1 now indexes the `if` itself (condition starts at g-2... chain walk below re-reads)
  }
  else
  {
    return false;
  }
  // walk the eqz/extend chain back to its leaf (starting at the insn right
  // before the `if` at g-1). `local.tee` may cache the mid-chain value
  // (pure — the value stays on the stack).
  std::size_t c = g - 1;
  int chain_len = 0;
  while ( c >= 1 && chain_len < 10
          && (insns[c - 1].opcode == 0x45 || insns[c - 1].opcode == 0x50
              || insns[c - 1].opcode == 0xAD || insns[c - 1].opcode == 0x22) )
  {
    --c;
    ++chain_len;
  }
  if ( chain_len == 0 || !cond_leaf_ok(c) )
    return false;

  out->guard_if_offset = insns[g - 1].offset;
  out->call_offset = insns[call_idx].offset;
  out->arity = arity;
  out->buf_local = buf_local;
  out->spill_count = spills;
  out->has_in_body_nil_guard = nil_guard;
  out->has_old_value_chain = old_chain;
  return true;
}

} // namespace

bool go_call_ir_t::pc_f_protocol_valid(std::uint32_t body_size) const
{
  if ( !is_pc_f_indirect )
    return false;
  if ( sp_cache_local == UINT32_MAX || call_offset >= body_size || sp_restore_offset >= body_size
       || pc_b_const_offset >= body_size )
    return false;
  for ( int k = 0; k < 5; ++k )
  {
    if ( pc_f_extract_offsets[k] >= body_size )
      return false;
    if ( k > 0 && pc_f_extract_offsets[k] <= pc_f_extract_offsets[k - 1] )
      return false;
  }
  if ( is_pc_f_fast_return )
  {
    if ( store_offset != UINT32_MAX || br_offset != UINT32_MAX || !args.empty() )
      return false;
    for ( int k = 0; k < 5; ++k )
      if ( sp_arith_offsets[k] != UINT32_MAX )
        return false;
    return pc_f_extract_offsets[3] < pc_b_const_offset
      && pc_b_const_offset < pc_f_extract_offsets[4] && pc_f_extract_offsets[4] < call_offset
      && call_offset < sp_restore_offset;
  }
  if ( store_offset >= body_size || br_offset >= body_size )
    return false;
  for ( int k = 0; k < 5; ++k )
  {
    if ( sp_arith_offsets[k] >= body_size )
      return false;
    if ( k > 0 && sp_arith_offsets[k] <= sp_arith_offsets[k - 1] )
      return false;
  }
  if ( !(sp_arith_offsets[4] < store_offset && store_offset < pc_f_extract_offsets[0]
         && pc_f_extract_offsets[3] < pc_b_const_offset
         && pc_b_const_offset < pc_f_extract_offsets[4] && pc_f_extract_offsets[4] < call_offset
         && call_offset < sp_restore_offset && sp_restore_offset < br_offset) )
    return false;
  for ( const go_arg_ir_t &arg : args )
    if ( arg.store_offset >= sp_arith_offsets[0] )
      return false;
  return true;
}

bool go_function_ir_t::pc_f_loop_valid(std::uint32_t body_size) const
{
  return present && is_pc_f_loop && pc_f_loop_call_offset < body_size && calls.empty()
    && sp_refresh.empty() && jmps.empty() && resume_table.dispatch_offset == UINT32_MAX;
}

bool go_prologue_ir_t::prologue_valid(std::uint32_t body_size) const
{
  if ( if_offset == UINT32_MAX )
    return false;
  if ( sp_get_offset >= body_size || guard_load_offset >= body_size || compare_offset >= body_size
       || store_offset >= body_size || call_offset >= body_size || br_if_offset >= body_size
       || end_offset >= body_size )
  {
    return false;
  }
  // Optional large-frame leg: absent (UINT32_MAX) or in-body before the
  // compare with the recorded framesize extra.
  if ( guard_const_offset != UINT32_MAX
       && (guard_const_offset >= body_size
           || (framesize_extra == 0 || guard_const_offset >= compare_offset)) )
  {
    return false;
  }
  // Strictly ordered evidence chain: SP read -> guard load [+ const] ->
  // compare -> if -> resume-PC store -> morestack call -> redo br_if -> end.
  if ( !(sp_get_offset < guard_load_offset && guard_load_offset < compare_offset
         && compare_offset < if_offset && if_offset < store_offset && store_offset < call_offset
         && call_offset < br_if_offset && br_if_offset < end_offset) )
    return false;
  // The proven call must be one of the recorded morestack call offsets.
  return true;
}

bool go_function_ir_t::frame_valid(std::uint32_t body_size) const
{
  if ( !present || frame_size == UINT32_MAX || frame_size == 0 )
    return false;
  if ( resume_table.dispatch_offset == UINT32_MAX || resume_table.dispatch_offset >= body_size )
    return false;
  if ( param_window >= kGoFrameWindowCap )
    return false;
  for ( const go_result_ir_t &result : results )
  {
    if ( result.store_offset >= body_size )
      return false;
    if ( result.sp_offset < param_window || result.sp_offset >= param_window + kGoFrameResultSpan )
      return false;
    if ( go_mem_op_width(uchar(result.opcode)) == 0 )
      return false;
  }
  return results.size() <= kGoFrameMaxResults;
}

bool go_result_slots_sorted(const go_function_ir_t &ir, std::vector<go_result_slot_t> *slots)
{
  if ( slots == nullptr || ir.results.empty() )
    return false;
  // A Go body stores the same result slot once per `return` site, so
  // identical (offset, width) pairs collapse. Overlapping distinct slots are
  // ambiguous — refuse rather than guess.
  slots->clear();
  slots->reserve(ir.results.size());
  for ( const go_result_ir_t &result : ir.results )
  {
    const std::uint32_t width = go_mem_op_width(uchar(result.opcode));
    if ( width == 0 )
      return false;
    go_result_slot_t slot;
    slot.sp_offset = result.sp_offset;
    slot.width = width;
    slot.opcode = result.opcode & 0xFFu;
    slots->push_back(slot);
  }
  std::sort(slots->begin(), slots->end(), [](const go_result_slot_t &a, const go_result_slot_t &b)
            { return a.sp_offset != b.sp_offset ? a.sp_offset < b.sp_offset : a.width < b.width; });
  slots->erase(std::unique(slots->begin(), slots->end(),
                           [](const go_result_slot_t &a, const go_result_slot_t &b)
                           { return a.sp_offset == b.sp_offset && a.width == b.width; }),
               slots->end());
  for ( std::size_t i = 1; i < slots->size(); ++i )
    if ( (*slots)[i].sp_offset < (*slots)[i - 1].sp_offset + (*slots)[i - 1].width )
      return false; // overlapping slots: ambiguous layout
  return true;
}

bool go_result_slot_types(const go_function_ir_t &ir, std::vector<unsigned char> *types)
{
  if ( types == nullptr )
    return false;
  std::vector<go_result_slot_t> slots;
  if ( !go_result_slots_sorted(ir, &slots) )
    return false;
  types->clear();
  types->reserve(slots.size());
  for ( const go_result_slot_t &slot : slots )
  {
    // Store opcode -> display type: i64/f64/f32 stores keep their type;
    // every narrower integer store (i32.store and the store8/store16/...
    // forms Go emits for bools and shorts) reads back as an i32.
    switch ( slot.opcode )
    {
      case 0x37: // i64.store
        types->push_back(0x7E);
        break;
      case 0x39: // f64.store
        types->push_back(0x7C);
        break;
      case 0x38: // f32.store
        types->push_back(0x7D);
        break;
      default: types->push_back(0x7F); break;
    }
  }
  return true;
}

bool go_arginfo_shape_params(const go_arginfo_t &arginfo, std::vector<go_shape_param_t> *params)
{
  if ( params == nullptr || !arginfo.present || arginfo.slots.empty()
       || arginfo.group_starts.size() != arginfo.slots.size() )
    return false;
  params->clear();
  std::size_t k = 0;
  while ( k < arginfo.slots.size() )
  {
    const go_arginfo_slot_t &slot = arginfo.slots[k];
    if ( slot.depth == 0 )
    {
      if ( arginfo.group_starts[k] != 0 )
        return false; // a flagged scalar: impossible encoding
      go_shape_param_t param;
      param.is_group = false;
      param.offset = slot.offset;
      param.bytes = slot.size;
      params->push_back(param);
      ++k;
      continue;
    }
    if ( arginfo.group_starts[k] == 0 )
      return false; // depth>=1 outside a group: cannot happen
    std::size_t end = k + 1;
    while ( end < arginfo.slots.size() && arginfo.slots[end].depth >= 1
            && arginfo.group_starts[end] == 0 )
      ++end;
    go_shape_param_t param;
    param.is_group = true;
    param.offset = arginfo.slots[k].offset;
    param.bytes = arginfo.slots[end - 1].offset + arginfo.slots[end - 1].size - param.offset;
    params->push_back(param);
    k = end;
  }
  return true;
}

go_sret_slot_verdict_t go_sret_result_slot_verdict(const module_t &module, std::size_t body_index)
{
  if ( !module.go_provenance || body_index >= module.bodies.size()
       || body_index >= module.defined_func_types.size() )
    return go_sret_slot_verdict_t::kNeutral;
  const body_t &body = module.bodies[body_index];
  const go_function_ir_t &ir = body.go_ir;
  if ( !ir.present || !ir.frame_valid(body.size) || ir.results.empty() )
    return go_sret_slot_verdict_t::kNeutral;
  // The phantom sret slot: make_multi_prototype appends `result` right
  // after the raw wasm parameters. Only the pure-register range is
  // analyzed — once arguments spill to the wasm user-register boundary the
  // two sides stop sharing slots and nothing positional can bind.
  // NOTE: types are indexed by TYPE index (a deduplicated table — balls
  // carries 11 types for 1779 functions), never by body index.
  if ( module.defined_func_types[body_index] >= module.types.size() )
    return go_sret_slot_verdict_t::kNeutral;
  const wasm_type_t &type = module.types[module.defined_func_types[body_index]];
  std::size_t phantom_slot = 0;
  for ( uchar param : type.params )
  {
    const std::size_t words = param == 0x7E || param == 0x7C ? 2 : 1;
    if ( phantom_slot + words > 6 )
      return go_sret_slot_verdict_t::kNeutral; // stack/pairing territory
    phantom_slot += words;
  }
  if ( phantom_slot >= 6 )
    return go_sret_slot_verdict_t::kNeutral;
  // The caller-side shape occupying the same register file.
  if ( body_index >= module.go_types.arginfo.size() )
    return go_sret_slot_verdict_t::kNeutral;
  std::vector<go_shape_param_t> params;
  if ( !go_arginfo_shape_params(module.go_types.arginfo[body_index], &params) )
    return go_sret_slot_verdict_t::kNeutral;
  const go_shape_param_t *bound = nullptr;
  std::size_t word = 0;
  for ( const go_shape_param_t &param : params )
  {
    const std::size_t words = param.is_group ? 1 : (param.bytes >= 8 ? 2 : 1);
    if ( word <= phantom_slot && phantom_slot < word + words )
    {
      bound = &param;
      break;
    }
    word += words;
    if ( word > phantom_slot )
      break;
  }
  if ( bound == nullptr )
    return go_sret_slot_verdict_t::kNeutral; // shape never reaches the slot
  // The proven result window start == the cell callers read post-call
  // (guaranteed by the caller read-coverage cross-check).
  std::vector<go_result_slot_t> slots;
  if ( !go_result_slots_sorted(ir, &slots) )
    return go_sret_slot_verdict_t::kNeutral;
  if ( bound->is_group && bound->offset == slots.front().sp_offset )
    return go_sret_slot_verdict_t::kAligned;
  return go_sret_slot_verdict_t::kBoundArgument;
}

bool go_function_ir_t::unflattening_valid(std::uint32_t body_size) const
{
  if ( !present || !pc_b_register_proof || resume_table.dispatch_offset == UINT32_MAX )
    return false;
  if ( resume_table.targets.size() < 2
       || resume_table.targets.size() != resume_table.depths.size() )
    return false;
  for ( std::uint32_t target : resume_table.targets )
    if ( target == 0 || target >= body_size )
      return false;
  for ( const go_jmp_ir_t &record : jmps )
  {
    if ( std::size_t(record.case_index) >= resume_table.targets.size() - 1 )
      return false;
  }
  return true;
}

void build_go_function_irs(module_t &module)
{
  // Clear any existing IRs.
  for ( body_t &body : module.bodies )
    body.go_ir = go_function_ir_t{};

  if ( !module.go_provenance )
    return;

  // Precompute per-callee incoming-argument window size for boundary
  // filtering: stores with memarg offset == 0 are always accepted (they either
  // store to `[SP + 0]` or follow an explicit `SP + const` address add);
  // stores with `memarg offset > 0` are accepted ONLY when the base is a plain
  // `local.get 1` (not a pointer loaded from the frame) and `sp_offset == 0`
  // (all nonzero memarg offsets in Go wasm are local-frame spills or struct
  // field writes, unless the caller allocated an explicit outgoing block in a
  // synthetic fixture without a Go callee frame).
  std::vector<std::uint32_t> callee_arg_window(module.bodies.size(), 0);
  for ( std::size_t i = 0; i < module.bodies.size(); ++i )
    callee_arg_window[i] = compute_callee_arg_window(module, i);

  // Locate pclntab-, name-section-, or bytecode-proven runtime.morestack[_noctxt]
  // indices.
  std::uint32_t morestack_fn1 = UINT32_MAX;
  std::uint32_t morestack_fn2 = UINT32_MAX;
  auto record_morestack_fn = [&](std::uint32_t fn)
  {
    if ( fn == UINT32_MAX || fn == morestack_fn1 || fn == morestack_fn2 )
      return;
    if ( morestack_fn1 == UINT32_MAX )
      morestack_fn1 = fn;
    else if ( morestack_fn2 == UINT32_MAX )
      morestack_fn2 = fn;
  };
  for ( std::size_t i = 0; i < module.bodies.size(); ++i )
  {
    const std::uint32_t fn_index = std::uint32_t(module.imported_func_types.size() + i);
    std::string resolved_name;
    for ( const export_t &entry : module.function_names )
      if ( entry.index == fn_index )
      {
        resolved_name = entry.name;
        break;
      }
    if ( resolved_name.empty() && i < module.pclntab_function_names.size() )
      resolved_name = module.pclntab_function_names[i];
    if ( is_morestack_function_name(resolved_name) )
      record_morestack_fn(fn_index);

    // Stripped-module fallback: match the 20-byte `runtime.morestack_noctxt`
    // wrapper body (`SP -> local 1; CTXT = 0; call morestack(0); SP -> local 1;
    // return; unreachable; end`) to recover both `morestack_noctxt` and its
    // `morestack` target when names are stripped.
    const body_t &body = module.bodies[i];
    if ( body.size == 20 && body.offset < module.code.size() )
    {
      const uchar *p = module.code.data() + body.offset;
      const std::size_t avail
        = std::min(std::size_t(body.size), module.code.size() - std::size_t(body.offset));
      binary::insn_cursor_t ms_cursor{ p, avail, 0, nullptr };
      std::vector<binary::decoded_insn_t> ms_insns;
      ms_insns.reserve(12);
      binary::decoded_insn_t ms_decoded;
      while ( ms_cursor.pos < avail && binary::next_insn(ms_cursor, &ms_decoded) )
        ms_insns.push_back(ms_decoded);
      std::uint32_t target_ms_fn = UINT32_MAX;
      if ( match_morestack_noctxt_wrapper_body(ms_insns, &target_ms_fn) )
      {
        record_morestack_fn(fn_index);
        record_morestack_fn(target_ms_fn);
      }
    }
  }

  const bool build_ir = profile_builds_go_ir(module.go_profile.profile);

  std::map<std::uint32_t, std::uint32_t> wb_arity_by_fn;
  std::set<std::uint32_t> sigpanic_fns;
  for ( std::size_t i = 0; i < module.bodies.size(); ++i )
  {
    const std::uint32_t fn_index = std::uint32_t(module.imported_func_types.size() + i);
    std::string resolved_name;
    for ( const export_t &entry : module.function_names )
      if ( entry.index == fn_index )
      {
        resolved_name = entry.name;
        break;
      }
    if ( resolved_name.empty() && i < module.pclntab_function_names.size() )
      resolved_name = module.pclntab_function_names[i];
    std::uint32_t arity = 0;
    if ( match_gc_write_barrier_name(resolved_name.c_str(), &arity) )
      wb_arity_by_fn.emplace(fn_index, arity);
    if ( match_sigpanic_name(resolved_name.c_str()) )
      sigpanic_fns.insert(fn_index);
  }
  // Stripped fallback: with no name anywhere, the nil-guard identity
  // falls back to the same structural terminal-noreturn proof the old
  // ctree consumer used (valid IR, unwind exit, PC_B proof, ZERO `return`
  // instructions, no backward const-JMP edge). Small bodies only — the
  // sigpanic dispatcher is tiny; this pre-pass is stripped-module-only.
  if ( sigpanic_fns.empty() && !module.bodies.empty() )
  {
    for ( std::size_t i = 0; i < module.bodies.size(); ++i )
    {
      const body_t &body = module.bodies[i];
      if ( body.size == 0 || body.size > 512 || body.offset >= module.code.size() )
        continue;
      const uchar *p = module.code.data() + body.offset;
      const std::size_t avail
        = std::min<std::size_t>(body.size, module.code.size() - std::size_t(body.offset));
      binary::insn_cursor_t c{ p, avail, 0, nullptr };
      std::vector<binary::decoded_insn_t> v;
      binary::decoded_insn_t d;
      while ( c.pos < avail && binary::next_insn(c, &d) )
        v.push_back(d);
      // Terminal-noreturn shape (the structural half of the old consumer's
      // sigpanic identity): no 0x0F return anywhere, a dispatcher block near
      // the body start, and every const-JMP (`i32.const k; local.set 0; br`)
      // resolves through the control records to a FORWARD target. Backward
      // edges (loops, like the near-miss helper) fail closed.
      if ( v.empty() || v.back().opcode != 0x0B )
        continue;
      bool saw_unwind = false;
      for ( std::size_t k = 0; k < v.size() && k < 8; ++k )
        if ( v[k].opcode == 0x02 )
          saw_unwind = true;
      if ( !saw_unwind )
        continue;
      bool clean = true;
      for ( std::size_t k = 0; k < v.size(); ++k )
      {
        if ( v[k].opcode == 0x0F )
        {
          clean = false;
          break;
        }
        if ( v[k].opcode == 0x0C && k >= 2 && v[k - 2].opcode == 0x41 && v[k - 1].opcode == 0x21
             && v[k - 1].imm[0] == 0 )
        {
          const body_t::control_t *ctrl = nullptr;
          for ( const body_t::control_t &candidate : body.control )
            if ( candidate.opcode == 0x0C && candidate.offset == v[k].offset )
            {
              ctrl = &candidate;
              break;
            }
          if ( ctrl == nullptr || ctrl->targets.empty() || ctrl->targets[0] <= v[k].offset )
          {
            clean = false; // unknown or backward edge
            break;
          }
        }
      }
      if ( clean )
        sigpanic_fns.insert(std::uint32_t(module.imported_func_types.size() + i));
    }
  }
  if ( wb_arity_by_fn.empty() )
  {
    // Stripped-module fallback: `i64.const N*8; call <core>; return;
    // unreachable; end` wrappers (6..16 bytes) around a core body matching
    // the wbBuf bump-allocation loop (60..256 bytes) — the exact two-level
    // proof the old ctree gc_write_barrier_index_t performed per database.
    std::map<std::uint32_t, bool> core_verified;
    auto core_ok = [&](std::uint32_t core_fn) -> bool
    {
      const auto cached = core_verified.find(core_fn);
      if ( cached != core_verified.end() )
        return cached->second;
      bool ok = false;
      const std::size_t core_i = std::size_t(core_fn < module.imported_func_types.size()
                                               ? UINT32_MAX
                                               : core_fn - module.imported_func_types.size());
      if ( core_i < module.bodies.size() )
      {
        const body_t &core_body = module.bodies[core_i];
        const std::uint32_t core_size = core_body.size;
        if ( core_body.offset < module.code.size() && core_size >= 60 && core_size <= 256 )
        {
          const std::size_t core_avail
            = std::min<std::size_t>(core_size, module.code.size() - std::size_t(core_body.offset));
          ok = match_gc_write_barrier_core_body(module.code.data() + core_body.offset, core_avail);
        }
      }
      core_verified.emplace(core_fn, ok);
      return ok;
    };
    for ( std::size_t i = 0; i < module.bodies.size(); ++i )
    {
      const body_t &body = module.bodies[i];
      if ( body.offset >= module.code.size() || body.size < 6 || body.size > 16 )
        continue;
      const std::size_t avail
        = std::min<std::size_t>(body.size, module.code.size() - std::size_t(body.offset));
      std::uint32_t arity = 0;
      std::uint32_t core_fn = UINT32_MAX;
      if ( !match_gc_write_barrier_wrapper_body(module.code.data() + body.offset, avail, &arity,
                                                &core_fn)
           || !core_ok(core_fn) )
      {
        continue;
      }
      wb_arity_by_fn.emplace(std::uint32_t(module.imported_func_types.size() + i), arity);
    }
  }

  for ( std::size_t i = 0; i < module.bodies.size(); ++i )
  {
    body_t &body = module.bodies[i];
    body.go_calls.clear();
    if ( i >= module.go_function_shapes.size() )
      continue;
    if ( body.offset >= module.code.size() || body.size == 0 )
      continue;
    const bool has_pc_b_sig = (module.go_function_shapes[i] & 1) != 0;
    const bool has_head_dispatch = (module.go_function_shapes[i] & 4) != 0;

    const uchar *p = module.code.data() + body.offset;
    const std::size_t avail
      = std::min(std::size_t(body.size), module.code.size() - std::size_t(body.offset));
    if ( !has_pc_b_sig && !has_head_dispatch )
    {
      if ( build_ir )
      {
        binary::insn_cursor_t loop_cursor{ p, avail, 0, nullptr };
        std::vector<binary::decoded_insn_t> loop_insns;
        loop_insns.reserve(32);
        binary::decoded_insn_t loop_decoded;
        while ( loop_cursor.pos < avail && binary::next_insn(loop_cursor, &loop_decoded) )
          loop_insns.push_back(loop_decoded);
        std::uint32_t loop_call_off = UINT32_MAX;
        if ( match_wasm_pc_f_loop_body(module, i, loop_insns, &loop_call_off) )
        {
          go_function_ir_t loop_ir;
          loop_ir.present = true;
          loop_ir.is_pc_f_loop = true;
          loop_ir.pc_f_loop_call_offset = loop_call_off;
          body.go_ir = std::move(loop_ir);
        }
        // Dispatcher-less Go bodies still carry write-barrier slow
        // paths — scan them for WB marks so the ctree layer treats every
        // body alike (the call-protocol IR below needs the dispatcher; the
        // WB protocol does not). A body whose only proven evidence is WB
        // sites still gets present=true: every other consumer gates on its
        // own fields (unwind/dispatcher/frame stay UINT32_MAX), while the
        // blob encoder refuses absent IRs entirely.
        if ( !wb_arity_by_fn.empty() )
        {
          for ( std::size_t k = 0;
                k < loop_insns.size() && body.go_ir.wb_sites.size() < kGoWbMaxSites; ++k )
          {
            if ( loop_insns[k].opcode != 0x10 || loop_insns[k].imm[0] > UINT32_MAX )
              continue;
            const auto wb_it = wb_arity_by_fn.find(std::uint32_t(loop_insns[k].imm[0]));
            if ( wb_it == wb_arity_by_fn.end() )
              continue;
            go_wb_site_ir_t site;
            if ( collect_go_wb_evidence(loop_insns, k, wb_it->second, sigpanic_fns, &site)
                 && site.wb_site_valid(body.size) )
              body.go_ir.wb_sites.push_back(site);
          }
          if ( !body.go_ir.wb_sites.empty() )
            body.go_ir.present = true;
        }
      }
      continue;
    }

    binary::insn_cursor_t cursor{ p, avail, 0, nullptr };
    std::vector<binary::decoded_insn_t> insns;
    insns.reserve(avail / 2);
    binary::decoded_insn_t decoded;
    while ( cursor.pos < avail && binary::next_insn(cursor, &decoded) )
      insns.push_back(decoded);

    go_function_ir_t ir;
    ir.present = build_ir;

    // 1. Identify unwind_exit, entry_point_loop, resume_table, jmps,
    //    morestack, ret, and pc_b_register_proof in a single pass over `insns`.
    if ( build_ir )
    {
      if ( insns.size() >= 3 && insns[insns.size() - 3].opcode == 0x0B
           && insns[insns.size() - 2].opcode == 0x41 && insns[insns.size() - 2].imm[0] == 1
           && insns[insns.size() - 1].opcode == 0x0B )
      {
        for ( std::size_t k = 0; k < std::min<std::size_t>(insns.size(), 8); ++k )
          if ( insns[k].opcode == 0x02 )
          {
            ir.unwind_exit = insns[k].offset;
            break;
          }
      }

      bool seen_br_table = false;
      bool pcb_clean = true;
      std::size_t first_morestack_idx = SIZE_MAX;
      for ( std::size_t k = 0; k < insns.size(); ++k )
      {
        const binary::decoded_insn_t &insn = insns[k];
        if ( !seen_br_table && insn.opcode == 0x03 && ir.entry_point_loop == UINT32_MAX )
          ir.entry_point_loop = insn.offset;

        if ( !seen_br_table && insn.opcode == 0x0E )
        {
          seen_br_table = true;
          const bool selector_local0
            = k >= 1 && insns[k - 1].opcode == 0x20 && insns[k - 1].imm[0] == 0;
          if ( selector_local0 )
          {
            for ( const body_t::control_t &ctrl : body.control )
            {
              if ( ctrl.opcode == 0x0E && ctrl.offset == insn.offset && !ctrl.targets.empty() )
              {
                ir.resume_table.dispatch_offset = insn.offset;
                ir.resume_table.targets = ctrl.targets;
                ir.resume_table.depths = ctrl.depths;
                if ( ir.resume_table.depths.size() < ir.resume_table.targets.size() )
                  ir.resume_table.depths.resize(ir.resume_table.targets.size(), 0);
                else if ( ir.resume_table.depths.size() > ir.resume_table.targets.size() )
                  ir.resume_table.depths.resize(ir.resume_table.targets.size());
                break;
              }
            }
          }
        }
        else if ( k >= 2 && insns[k - 2].opcode == 0x41 && insns[k - 1].opcode == 0x21
                  && insns[k - 1].imm[0] == 0 && insn.opcode == 0x0C )
        {
          go_jmp_ir_t jmp;
          jmp.const_offset = insns[k - 2].offset;
          jmp.set_offset = insns[k - 1].offset;
          jmp.br_offset = insn.offset;
          jmp.case_index = std::uint32_t(insns[k - 2].imm[0]);
          ir.jmps.push_back(jmp);
        }

        if ( insn.opcode == 0x10 && (insn.imm[0] == morestack_fn1 || insn.imm[0] == morestack_fn2) )
        {
          ir.morestack.push_back(insn.offset);
          // G10: remember the FIRST proven morestack call index — the Go
          // compiler emits exactly one stack check per body, so the prologue
          // is proven around it right after the pass.
          if ( first_morestack_idx == SIZE_MAX )
            first_morestack_idx = k;
        }

        if ( insn.opcode == 0x0F )
        {
          ir.ret.push_back(insn.offset);
        }

        // Check PC_B register proof:
        // Reads (`local.get 0`) must be either the dispatcher selector
        // (`local.get 0; br_table`) or an indirect-call table index
        // (`local.get 0; call_indirect`).
        if ( insn.opcode == 0x20 && insn.imm[0] == 0 )
        {
          const bool is_dispatch = k + 1 < insns.size() && insns[k + 1].opcode == 0x0E;
          const bool is_indirect_call = k + 1 < insns.size() && insns[k + 1].opcode == 0x11;
          if ( !is_dispatch && !is_indirect_call )
            pcb_clean = false;
        }
        // Writes (`local.set 0` / `local.tee 0`) must be either a const-JMP
        // (`i32.const k; local.set 0; br`), an indirect-call PC_F extraction
        // (`i32.wrap_i64; local.set 0`), or an SP=PC_B write (`local.get 1;
        // local.set 0`).
        if ( (insn.opcode == 0x21 || insn.opcode == 0x22) && insn.imm[0] == 0 )
        {
          const bool is_const_jmp = insn.opcode == 0x21 && k >= 1 && insns[k - 1].opcode == 0x41
            && k + 1 < insns.size() && insns[k + 1].opcode == 0x0C;
          const bool is_ind_temp = k >= 1 && insns[k - 1].opcode == 0xA7;
          const bool is_sp_temp = k >= 1 && insns[k - 1].opcode == 0x20 && insns[k - 1].imm[0] == 1;
          if ( !is_const_jmp && !is_ind_temp && !is_sp_temp )
            pcb_clean = false;
        }
      }
      ir.pc_b_register_proof = (ir.resume_table.dispatch_offset != UINT32_MAX) && pcb_clean;

      // 1b. G10: prove the stack-check prologue around the first morestack
      // call. Fail-closed — a body whose prologue shape does not match the
      // compiler sequence keeps if_offset at UINT32_MAX and the ctree layer
      // never hides its guard.
      if ( first_morestack_idx != SIZE_MAX )
        collect_go_prologue_evidence(insns, first_morestack_idx, &ir.prologue);

      // 1c. Prove every write-barrier slow path around a call to a
      // gcWriteBarrier{1..8} callee. Fail-closed per site — a protocol
      // mismatch leaves the site unmarked and the ctree layer never touches
      // its guard.
      if ( !wb_arity_by_fn.empty() )
      {
        for ( std::size_t k = 0; k < insns.size() && ir.wb_sites.size() < kGoWbMaxSites; ++k )
        {
          if ( insns[k].opcode != 0x10 || insns[k].imm[0] > UINT32_MAX )
            continue;
          const auto wb_it = wb_arity_by_fn.find(std::uint32_t(insns[k].imm[0]));
          if ( wb_it == wb_arity_by_fn.end() )
            continue;
          go_wb_site_ir_t site;
          if ( collect_go_wb_evidence(insns, k, wb_it->second, sigpanic_fns, &site)
               && site.wb_site_valid(body.size) )
            ir.wb_sites.push_back(site);
        }
      }
    }

    // 2. Scan Go calls + argument-collection boundary + SP-arith +
    //    SP-refresh pairs (for functions with head dispatch or a verified
    //    Go IR resume table). Under a verified modern Go profile
    //    (`build_ir`), also scan Go indirect calls through `PC_F`
    //    (`i64.const 16; i64.shr_u; i32.wrap_i64; local.set 0; i32.const 0;
    //     local.get 0; call_indirect 0 0`).
    const bool scan_calls
      = has_head_dispatch || (build_ir && ir.resume_table.dispatch_offset != UINT32_MAX);
    for ( std::size_t idx = 0; scan_calls && idx + 9 < insns.size(); ++idx )
    {
      if ( insns[idx].opcode != 0x22 || insns[idx].imm[0] != 1 || insns[idx + 1].opcode != 0x24
           || insns[idx + 1].imm[0] != 0 || insns[idx + 2].opcode != 0x20
           || insns[idx + 2].imm[0] != 1 || insns[idx + 3].opcode != 0x42
           || insns[idx + 4].opcode != 0x37 || insns[idx + 4].imm[0] != 3
           || insns[idx + 4].imm[1] != 0 )
      {
        continue;
      }

      const bool is_direct = insns[idx + 5].opcode == 0x41 && insns[idx + 5].imm[0] == 0
        && insns[idx + 6].opcode == 0x10 && insns[idx + 7].opcode == 0x23
        && insns[idx + 7].imm[0] == 0 && insns[idx + 8].opcode == 0x21 && insns[idx + 8].imm[0] == 1
        && insns[idx + 9].opcode == 0x0D;

      const bool is_pcf_indirect = !is_direct && build_ir && idx + 14 < insns.size()
        && insns[idx + 5].opcode == 0x42 && insns[idx + 5].imm[0] == 16
        && insns[idx + 6].opcode == 0x88 && insns[idx + 7].opcode == 0xA7
        && insns[idx + 8].opcode == 0x21 && insns[idx + 8].imm[0] == 0
        && insns[idx + 9].opcode == 0x41 && insns[idx + 9].imm[0] == 0
        && insns[idx + 10].opcode == 0x20 && insns[idx + 10].imm[0] == 0
        && insns[idx + 11].opcode == 0x11 && insns[idx + 11].imm[1] == 0
        && insns[idx + 11].imm[0] < module.types.size()
        && module.types[insns[idx + 11].imm[0]].params.size() == 1
        && module.types[insns[idx + 11].imm[0]].params[0] == 0x7F
        && module.types[insns[idx + 11].imm[0]].results.size() == 1
        && module.types[insns[idx + 11].imm[0]].results[0] == 0x7F && insns[idx + 12].opcode == 0x23
        && insns[idx + 12].imm[0] == 0 && insns[idx + 13].opcode == 0x21
        && insns[idx + 13].imm[0] == 1 && insns[idx + 14].opcode == 0x0D;

      if ( !is_direct && !is_pcf_indirect )
        continue;

      std::uint32_t fn = UINT32_MAX;
      std::uint32_t callee_window = 0;
      if ( is_direct )
      {
        const std::uint64_t raw_fn = insns[idx + 6].imm[0];
        if ( raw_fn >= module.imported_func_types.size() + module.bodies.size() )
          continue;
        fn = std::uint32_t(raw_fn);
        if ( fn >= module.imported_func_types.size() )
        {
          const std::size_t callee_bi = std::size_t(fn - module.imported_func_types.size());
          if ( callee_bi < callee_arg_window.size() )
            callee_window = callee_arg_window[callee_bi];
        }
      }

      const std::size_t call_idx = is_direct ? (idx + 6) : (idx + 11);
      const std::size_t pcb_const_idx = is_direct ? (idx + 5) : (idx + 9);
      const std::size_t refresh_get_idx = is_direct ? (idx + 7) : (idx + 12);
      const std::size_t refresh_set_idx = is_direct ? (idx + 8) : (idx + 13);
      const std::size_t br_idx = is_direct ? (idx + 9) : (idx + 14);

      body_t::go_call_t legacy_entry;
      legacy_entry.store_offset = insns[idx + 4].offset;
      legacy_entry.call_offset = insns[call_idx].offset;
      legacy_entry.fn_index = fn;
      legacy_entry.sp_restore_offset = insns[refresh_get_idx].offset;
      legacy_entry.br_offset = insns[br_idx].offset;

      go_call_ir_t call_ir;
      call_ir.store_offset = legacy_entry.store_offset;
      call_ir.call_offset = legacy_entry.call_offset;
      call_ir.fn_index = legacy_entry.fn_index;
      call_ir.sp_restore_offset = legacy_entry.sp_restore_offset;
      call_ir.br_offset = legacy_entry.br_offset;
      call_ir.pc_b_const_offset = insns[pcb_const_idx].offset;
      call_ir.is_pc_f_indirect = is_pcf_indirect;
      if ( is_pcf_indirect )
      {
        call_ir.pc_f_extract_offsets[0] = insns[idx + 5].offset;
        call_ir.pc_f_extract_offsets[1] = insns[idx + 6].offset;
        call_ir.pc_f_extract_offsets[2] = insns[idx + 7].offset;
        call_ir.pc_f_extract_offsets[3] = insns[idx + 8].offset;
        call_ir.pc_f_extract_offsets[4] = insns[idx + 10].offset;
      }

      if ( idx >= 3 && insns[idx - 1].opcode == 0x6B && insns[idx - 2].opcode == 0x41
           && insns[idx - 3].opcode == 0x20 && insns[idx - 3].imm[0] == 1 )
      {
        call_ir.sp_arith_offsets[0] = insns[idx - 3].offset;
        call_ir.sp_arith_offsets[1] = insns[idx - 2].offset;
        call_ir.sp_arith_offsets[2] = insns[idx - 1].offset;
        call_ir.sp_arith_offsets[3] = insns[idx].offset;
        call_ir.sp_arith_offsets[4] = insns[idx + 1].offset;
        call_ir.sp_cache_local = 1;

        std::size_t k_idx = idx - 3;
        if ( is_pcf_indirect )
        {
          // Before `SP -= 8` (at `idx - 3`), a Go indirect call pushes the
          // 64-bit funcval target on the operand stack, preceded by an
          // optional `global.set 1` (CTXT) closure context write. Check if
          // the target is a provably-constant funcval and advance `k_idx`
          // past the target push and CTXT write to reach the outgoing-arg
          // stores.
          std::size_t target_begin = k_idx;
          std::uint32_t proven_fn = UINT32_MAX;
          if ( try_resolve_constant_pc_f_callee(module, insns, k_idx, &target_begin, &proven_fn) )
          {
            call_ir.fn_index = proven_fn;
            legacy_entry.fn_index = proven_fn;
            if ( proven_fn >= module.imported_func_types.size() )
            {
              const std::size_t callee_bi
                = std::size_t(proven_fn - module.imported_func_types.size());
              if ( callee_bi < callee_arg_window.size() )
                callee_window = callee_arg_window[callee_bi];
            }
          }
          k_idx = target_begin;
          // Skip optional `global.set 1` (CTXT) closure context write.
          if ( k_idx >= 1 && insns[k_idx - 1].opcode == 0x24 && insns[k_idx - 1].imm[0] == 1 )
          {
            if ( k_idx >= 2
                 && (insns[k_idx - 2].opcode == 0x20 || insns[k_idx - 2].opcode == 0x42) )
              k_idx -= 2;
            else if ( k_idx >= 3 && insns[k_idx - 2].opcode == 0x29
                      && insns[k_idx - 3].opcode == 0x20 )
              k_idx -= 3;
            else if ( k_idx >= 5 && insns[k_idx - 2].opcode == 0x7C
                      && insns[k_idx - 3].opcode == 0x42 && insns[k_idx - 4].opcode == 0xAD
                      && insns[k_idx - 5].opcode == 0x20 )
              k_idx -= 5;
          }
          // Skip compiler-inserted `nop` instructions (e.g. between local
          // spills and argument stores).
          while ( k_idx >= 1 && insns[k_idx - 1].opcode == 0x01 )
            --k_idx;
        }

        static const uchar arg_store_aligns[4] = { 3, 2, 2, 3 };
        static const uchar arg_store_ops[4]
          = { k_ir_i64_store, k_ir_i32_store, k_ir_f32_store, k_ir_f64_store };
        while ( k_idx >= 2 )
        {
          const binary::decoded_insn_t &store_insn = insns[k_idx - 1];
          int store_kind = -1;
          for ( int candidate = 0; candidate < 4; ++candidate )
          {
            if ( store_insn.opcode == arg_store_ops[candidate]
                 && store_insn.imm[0] == arg_store_aligns[candidate] )
            {
              store_kind = candidate;
              break;
            }
          }
          if ( store_kind < 0 || store_insn.imm[1] > UINT32_MAX )
            break;

          std::size_t base_idx = SIZE_MAX;
          for ( std::size_t back = k_idx - 1; back > 0; )
          {
            --back;
            const uchar back_op = insns[back].opcode;
            // Stop at a structured-control boundary (`block`, `loop`, `if`,
            // `else`, `end`, `br`, `br_if`, `br_table`, `return`) so a heap/
            // global store after a write-barrier `end` never claims the
            // write-barrier block's inner `local.get 1` as its SP base and
            // leaves the actual pointer address stranded on the operand stack
            // at the preceding `end`.
            if ( back_op == 0x02 || back_op == 0x03 || back_op == 0x04 || back_op == 0x05
                 || back_op == 0x0B || back_op == 0x0C || back_op == 0x0D || back_op == 0x0E
                 || back_op == 0x0F )
              break;
            if ( back_op == 0x20 && insns[back].imm[0] == 1 )
            {
              base_idx = back;
              break;
            }
          }
          if ( base_idx == SIZE_MAX || base_idx + 1 >= k_idx - 1 )
            break;

          const std::uint32_t sp_off = std::uint32_t(store_insn.imm[1]);
          // Boundary check:
          // When collecting call arguments, only accept stores whose offset
          // falls inside the callee's outgoing-argument window per its
          // signature; stores to the caller's local frame area (large offsets
          // like 136, 168) must render as separate statements. For dynamic
          // indirect calls where the callee frame window is unknown, only
          // `sp_off == 0` (`[SP + 0]` or `SP + const` with `memarg_offset == 0`)
          // is an outgoing argument store.
          if ( is_pcf_indirect && call_ir.fn_index == UINT32_MAX && sp_off > 0 )
            break;
          if ( sp_off >= 128 && sp_off >= std::max(callee_window, 16u) )
            break;

          body_t::go_arg_t arg;
          arg.opcode = store_insn.opcode;
          arg.sp_offset = sp_off;
          arg.store_offset = store_insn.offset;
          legacy_entry.args.push_back(arg);

          go_arg_ir_t arg_ir;
          arg_ir.opcode = arg.opcode;
          arg_ir.sp_offset = arg.sp_offset;
          arg_ir.store_offset = arg.store_offset;
          call_ir.args.push_back(arg_ir);

          k_idx = base_idx;
        }
        std::reverse(legacy_entry.args.begin(), legacy_entry.args.end());
        std::reverse(call_ir.args.begin(), call_ir.args.end());
      }

      if ( is_direct && has_head_dispatch )
        body.go_calls.push_back(legacy_entry);
      if ( build_ir )
      {
        ir.calls.push_back(call_ir);
        go_sp_refresh_ir_t refresh;
        refresh.get_offset = insns[refresh_get_idx].offset;
        refresh.set_offset = insns[refresh_set_idx].offset;
        refresh.local_index = std::uint32_t(insns[refresh_set_idx].imm[0]);
        ir.sp_refresh.push_back(refresh);
      }
      idx = br_idx;
    }

    // 3. Scan tail/fast-path PC_F indirect calls (`runtime.systemstack`
    //    on-g0 fast path) that execute on the caller's stack frame without
    //    `SP -= 8; i64.store ret_pc; ...; br_if unwindExit`:
    //      global.set 1 (CTXT);
    //      global.get 1; i32.wrap_i64; i64.load align=3 off=0;
    //      i64.const 16; i64.shr_u; i32.wrap_i64; local.set 0;
    //      i32.const 0; local.get 0; call_indirect 0 0;
    //      global.get 0; local.set 1; return
    if ( build_ir && scan_calls && ir.pc_b_register_proof )
    {
      for ( std::size_t idx = 4; idx + 9 < insns.size(); ++idx )
      {
        const bool is_fast_ret = insns[idx - 4].opcode == 0x24 && insns[idx - 4].imm[0] == 1
          && insns[idx - 3].opcode == 0x23 && insns[idx - 3].imm[0] == 1
          && insns[idx - 2].opcode == 0xA7 && insns[idx - 1].opcode == 0x29
          && insns[idx - 1].imm[0] == 3 && insns[idx - 1].imm[1] == 0 && insns[idx].opcode == 0x42
          && insns[idx].imm[0] == 16 && insns[idx + 1].opcode == 0x88
          && insns[idx + 2].opcode == 0xA7 && insns[idx + 3].opcode == 0x21
          && insns[idx + 3].imm[0] == 0 && insns[idx + 4].opcode == 0x41
          && insns[idx + 4].imm[0] == 0 && insns[idx + 5].opcode == 0x20
          && insns[idx + 5].imm[0] == 0 && insns[idx + 6].opcode == 0x11
          && insns[idx + 6].imm[1] == 0 && insns[idx + 6].imm[0] < module.types.size()
          && module.types[insns[idx + 6].imm[0]].params.size() == 1
          && module.types[insns[idx + 6].imm[0]].params[0] == 0x7F
          && module.types[insns[idx + 6].imm[0]].results.size() == 1
          && module.types[insns[idx + 6].imm[0]].results[0] == 0x7F && insns[idx + 7].opcode == 0x23
          && insns[idx + 7].imm[0] == 0 && insns[idx + 8].opcode == 0x21
          && insns[idx + 8].imm[0] == 1 && insns[idx + 9].opcode == 0x0F;
        if ( !is_fast_ret )
          continue;

        go_call_ir_t fast_call;
        fast_call.call_offset = insns[idx + 6].offset;
        fast_call.fn_index = UINT32_MAX;
        fast_call.sp_restore_offset = insns[idx + 7].offset;
        fast_call.pc_b_const_offset = insns[idx + 4].offset;
        fast_call.sp_cache_local = 1;
        fast_call.is_pc_f_indirect = true;
        fast_call.is_pc_f_fast_return = true;
        fast_call.pc_f_extract_offsets[0] = insns[idx].offset;
        fast_call.pc_f_extract_offsets[1] = insns[idx + 1].offset;
        fast_call.pc_f_extract_offsets[2] = insns[idx + 2].offset;
        fast_call.pc_f_extract_offsets[3] = insns[idx + 3].offset;
        fast_call.pc_f_extract_offsets[4] = insns[idx + 5].offset;
        ir.calls.push_back(fast_call);

        go_sp_refresh_ir_t refresh;
        refresh.get_offset = insns[idx + 7].offset;
        refresh.set_offset = insns[idx + 8].offset;
        refresh.local_index = 1;
        ir.sp_refresh.push_back(refresh);

        idx += 9;
      }
    }

    if ( build_ir )
    {
      // Frame evidence (framesize / param window / result slots) for
      // every body that reached IR building. Fail-closed: collect leaves
      // frame_size at UINT32_MAX when the prologue is unproven or a store
      // writes back into the param window.
      collect_go_frame_evidence(insns, ir);
      body.go_ir = std::move(ir);
    }
  }

  // Caller cross-check over the freshly built result records (needs
  // every callee's frame evidence AND every caller's call records, so it runs
  // after the per-body loop). Only marks `results_caller_proven`.
  if ( build_ir )
    cross_check_go_result_callers(module);

  // Build the module-level set of structurally proven terminal noreturn
  // functions and mark `call.is_noreturn_call` on every standard Go call
  // targeting them.
  if ( build_ir )
  {
    const std::size_t total_funcs = module.imported_func_types.size() + module.bodies.size();
    std::vector<bool> noreturn_fn_set(total_funcs, false);
    for ( std::size_t i = 0; i < module.bodies.size(); ++i )
    {
      const std::uint32_t fn_index = std::uint32_t(module.imported_func_types.size() + i);
      const bool is_ms = (fn_index == morestack_fn1 || fn_index == morestack_fn2);
      if ( is_go_terminal_noreturn_function(module.bodies[i].go_ir, is_ms) )
      {
        noreturn_fn_set[fn_index] = true;
      }
    }
    for ( body_t &body : module.bodies )
    {
      if ( !body.go_ir.present )
        continue;
      for ( go_call_ir_t &call : body.go_ir.calls )
      {
        if ( !call.is_pc_f_fast_return && call.fn_index < noreturn_fn_set.size()
             && noreturn_fn_set[call.fn_index] )
        {
          call.is_noreturn_call = true;
        }
      }
    }
  }
}

void encode_go_function_ir_blob(const go_function_ir_t &ir, std::vector<std::uint32_t> *out)
{
  out->clear();
  if ( !ir.present )
    return;
  // Prefix is 100% compatible with v2 `[2][call_count][call_records...]` so
  // existing v2 consumers and corruption tests work unchanged.
  out->push_back(2u);
  out->push_back(std::uint32_t(ir.calls.size()));
  for ( const go_call_ir_t &call : ir.calls )
  {
    out->push_back(call.store_offset);
    out->push_back(call.call_offset);
    out->push_back(call.fn_index);
    out->push_back(call.sp_restore_offset);
    out->push_back(call.br_offset);
    out->push_back(std::uint32_t(call.args.size()));
    for ( const go_arg_ir_t &arg : call.args )
    {
      out->push_back(arg.opcode);
      out->push_back(arg.sp_offset);
      out->push_back(arg.store_offset);
    }
  }
  // Append the versioned IR tail.
  out->push_back(kGoIrTailMagic);
  out->push_back(kGoIrTailVersion);
  out->push_back(ir.unwind_exit);
  out->push_back(ir.entry_point_loop);
  out->push_back(ir.resume_table.dispatch_offset);
  out->push_back(ir.pc_b_register_proof ? 1u : 0u);
  out->push_back(std::uint32_t(ir.resume_table.targets.size()));
  for ( std::size_t i = 0; i < ir.resume_table.targets.size(); ++i )
  {
    out->push_back(ir.resume_table.targets[i]);
    out->push_back(i < ir.resume_table.depths.size() ? ir.resume_table.depths[i] : 0u);
  }
  out->push_back(std::uint32_t(ir.jmps.size()));
  for ( const go_jmp_ir_t &jmp : ir.jmps )
  {
    out->push_back(jmp.const_offset);
    out->push_back(jmp.set_offset);
    out->push_back(jmp.br_offset);
    out->push_back(jmp.case_index);
  }
  out->push_back(std::uint32_t(ir.calls.size()));
  for ( const go_call_ir_t &call : ir.calls )
  {
    out->push_back(call.pc_b_const_offset);
    for ( int k = 0; k < 5; ++k )
      out->push_back(call.sp_arith_offsets[k]);
    out->push_back(call.sp_cache_local);
  }
  out->push_back(std::uint32_t(ir.sp_refresh.size()));
  for ( const go_sp_refresh_ir_t &ref : ir.sp_refresh )
  {
    out->push_back(ref.get_offset);
    out->push_back(ref.set_offset);
    out->push_back(ref.local_index);
  }
  out->push_back(std::uint32_t(ir.morestack.size()));
  for ( std::uint32_t off : ir.morestack )
    out->push_back(off);
  out->push_back(std::uint32_t(ir.ret.size()));
  for ( std::uint32_t off : ir.ret )
    out->push_back(off);

  // Append PC_F indirect-call / noreturn-call tail when any call record
  // is a PC_F `call_indirect` or a proven noreturn panic call
  // (backward-compatible with v3 readers/blobs that have neither).
  bool any_pcf = false;
  for ( const go_call_ir_t &call : ir.calls )
    if ( call.is_pc_f_indirect || call.is_noreturn_call )
    {
      any_pcf = true;
      break;
    }
  if ( any_pcf )
  {
    out->push_back(kGoIrPcfTailMagic);
    out->push_back(kGoIrPcfTailVersion);
    out->push_back(std::uint32_t(ir.calls.size()));
    for ( const go_call_ir_t &call : ir.calls )
    {
      const std::uint32_t pcf_flags = (call.is_pc_f_indirect ? 1u : 0u)
        | (call.is_pc_f_fast_return ? 2u : 0u) | (call.is_noreturn_call ? 4u : 0u);
      out->push_back(pcf_flags);
      for ( int k = 0; k < 5; ++k )
        out->push_back(call.pc_f_extract_offsets[k]);
    }
  }
  if ( ir.is_pc_f_loop )
  {
    out->push_back(kGoIrLoopTailMagic);
    out->push_back(kGoIrLoopTailVersion);
    out->push_back(ir.pc_f_loop_call_offset);
  }
  // Append the frame-evidence tail when the frame is proven
  // (backward-compatible with readers that stop at GOPC/GOLP). v2 adds the
  // Caller-cross-check verdict word after the result records.
  if ( ir.frame_size != UINT32_MAX )
  {
    out->push_back(kGoIrFrameTailMagic);
    out->push_back(kGoIrFrameTailVersion);
    out->push_back(ir.frame_size);
    out->push_back(ir.param_window);
    out->push_back(std::uint32_t(ir.results.size()));
    for ( const go_result_ir_t &result : ir.results )
    {
      out->push_back(result.opcode);
      out->push_back(result.sp_offset);
      out->push_back(result.store_offset);
    }
    out->push_back(ir.results_caller_state);
  }
  // G10: append the morestack stack-check prologue tail when proven
  // (backward-compatible with readers that stop at the earlier tails).
  if ( ir.prologue.if_offset != UINT32_MAX )
  {
    out->push_back(kGoIrPrologueTailMagic);
    out->push_back(kGoIrPrologueTailVersion);
    out->push_back(ir.prologue.sp_get_offset);
    out->push_back(ir.prologue.guard_load_offset);
    out->push_back(ir.prologue.guard_const_offset);
    out->push_back(ir.prologue.framesize_extra);
    out->push_back(ir.prologue.compare_offset);
    out->push_back(ir.prologue.if_offset);
    out->push_back(ir.prologue.store_offset);
    out->push_back(ir.prologue.call_offset);
    out->push_back(ir.prologue.br_if_offset);
    out->push_back(ir.prologue.end_offset);
  }
  // Append the write-barrier site tail when sites are proven
  // (backward-compatible with readers that stop at the earlier tails).
  if ( !ir.wb_sites.empty() )
  {
    out->push_back(kGoIrWbTailMagic);
    out->push_back(kGoIrWbTailVersion);
    out->push_back(std::uint32_t(ir.wb_sites.size()));
    for ( const go_wb_site_ir_t &site : ir.wb_sites )
    {
      out->push_back(site.guard_if_offset);
      out->push_back(site.call_offset);
      out->push_back(site.arity);
      out->push_back(site.buf_local);
      out->push_back(site.spill_count);
      out->push_back((site.has_in_body_nil_guard ? 1u : 0u) | (site.has_old_value_chain ? 2u : 0u));
    }
  }
}

bool decode_go_function_ir_blob(const std::uint32_t *words, std::size_t word_count,
                                go_function_ir_t *out)
{
  if ( out == nullptr )
    return false;
  *out = go_function_ir_t{};
  if ( words == nullptr || word_count < 2 || words[0] != 2u )
    return false;
  const std::size_t call_count = std::size_t(words[1]);
  if ( call_count > WASMHEX_MAX_GO_CALL_RECORDS )
    return false;

  std::size_t cursor = 2;
  out->calls.resize(call_count);
  for ( std::size_t r = 0; r < call_count; ++r )
  {
    if ( cursor + 6 > word_count )
      return false;
    go_call_ir_t &call = out->calls[r];
    call.store_offset = words[cursor + 0];
    call.call_offset = words[cursor + 1];
    call.fn_index = words[cursor + 2];
    call.sp_restore_offset = words[cursor + 3];
    call.br_offset = words[cursor + 4];
    const std::size_t arg_count = std::size_t(words[cursor + 5]);
    cursor += 6;
    if ( arg_count > (word_count - cursor) / 3 )
      return false;
    call.args.resize(arg_count);
    for ( std::size_t a = 0; a < arg_count; ++a )
    {
      call.args[a].opcode = words[cursor + 0];
      call.args[a].sp_offset = words[cursor + 1];
      call.args[a].store_offset = words[cursor + 2];
      cursor += 3;
    }
    if ( call.call_offset >= 2 )
      call.pc_b_const_offset = call.call_offset - 2;
  }

  // Legacy v2 blob without IR tail: valid calls, `present = false`.
  if ( cursor == word_count )
    return true;

  if ( cursor + 7 > word_count || words[cursor] != kGoIrTailMagic
       || words[cursor + 1] != kGoIrTailVersion )
    return false;
  cursor += 2;

  out->present = true;
  out->unwind_exit = words[cursor++];
  out->entry_point_loop = words[cursor++];
  out->resume_table.dispatch_offset = words[cursor++];
  out->pc_b_register_proof = (words[cursor++] != 0);

  const std::size_t target_count = std::size_t(words[cursor++]);
  if ( target_count > (word_count - cursor) / 2 )
    return false;
  out->resume_table.targets.resize(target_count);
  out->resume_table.depths.resize(target_count);
  for ( std::size_t i = 0; i < target_count; ++i )
  {
    out->resume_table.targets[i] = words[cursor++];
    out->resume_table.depths[i] = words[cursor++];
  }

  if ( cursor >= word_count )
    return false;
  const std::size_t jmp_count = std::size_t(words[cursor++]);
  if ( jmp_count > (word_count - cursor) / 4 )
    return false;
  out->jmps.resize(jmp_count);
  for ( std::size_t i = 0; i < jmp_count; ++i )
  {
    out->jmps[i].const_offset = words[cursor++];
    out->jmps[i].set_offset = words[cursor++];
    out->jmps[i].br_offset = words[cursor++];
    out->jmps[i].case_index = words[cursor++];
  }

  if ( cursor >= word_count )
    return false;
  const std::size_t ext_call_count = std::size_t(words[cursor++]);
  if ( ext_call_count != call_count || ext_call_count > (word_count - cursor) / 7 )
    return false;
  for ( std::size_t i = 0; i < ext_call_count; ++i )
  {
    out->calls[i].pc_b_const_offset = words[cursor++];
    for ( int k = 0; k < 5; ++k )
      out->calls[i].sp_arith_offsets[k] = words[cursor++];
    out->calls[i].sp_cache_local = words[cursor++];
  }

  if ( cursor >= word_count )
    return false;
  const std::size_t refresh_count = std::size_t(words[cursor++]);
  if ( refresh_count != call_count || refresh_count > (word_count - cursor) / 3 )
    return false;
  out->sp_refresh.resize(refresh_count);
  for ( std::size_t i = 0; i < refresh_count; ++i )
  {
    out->sp_refresh[i].get_offset = words[cursor++];
    out->sp_refresh[i].set_offset = words[cursor++];
    out->sp_refresh[i].local_index = words[cursor++];
  }

  if ( cursor >= word_count )
    return false;
  const std::size_t ms_count = std::size_t(words[cursor++]);
  if ( ms_count > word_count - cursor )
    return false;
  out->morestack.resize(ms_count);
  for ( std::size_t i = 0; i < ms_count; ++i )
    out->morestack[i] = words[cursor++];

  if ( cursor >= word_count )
    return false;
  const std::size_t ret_count = std::size_t(words[cursor++]);
  if ( ret_count > word_count - cursor )
    return false;
  out->ret.resize(ret_count);
  for ( std::size_t i = 0; i < ret_count; ++i )
    out->ret[i] = words[cursor++];

  // Optional PC_F indirect-call tail and/or wasm_pc_f_loop tail:
  // absent when the function has only direct Go calls.
  if ( cursor == word_count )
    return true;
  if ( cursor + 3 <= word_count && words[cursor] == kGoIrPcfTailMagic
       && words[cursor + 1] == kGoIrPcfTailVersion )
  {
    cursor += 2;
    const std::size_t pcf_call_count = std::size_t(words[cursor++]);
    if ( pcf_call_count != call_count || pcf_call_count > (word_count - cursor) / 6 )
      return false;
    for ( std::size_t i = 0; i < pcf_call_count; ++i )
    {
      const std::uint32_t pcf_flags = words[cursor++];
      if ( (pcf_flags & ~7u) != 0 || ((pcf_flags & 2u) != 0 && (pcf_flags & 1u) == 0) )
        return false;
      out->calls[i].is_pc_f_indirect = (pcf_flags & 1u) != 0;
      out->calls[i].is_pc_f_fast_return = (pcf_flags & 2u) != 0;
      out->calls[i].is_noreturn_call = (pcf_flags & 4u) != 0;
      for ( int k = 0; k < 5; ++k )
        out->calls[i].pc_f_extract_offsets[k] = words[cursor++];
    }
  }
  if ( cursor == word_count )
    return true;
  if ( cursor + 3 <= word_count && words[cursor] == kGoIrLoopTailMagic
       && words[cursor + 1] == kGoIrLoopTailVersion )
  {
    cursor += 2;
    out->is_pc_f_loop = true;
    out->pc_f_loop_call_offset = words[cursor++];
  }

  // Optional frame-evidence tail: absent when the frame was not proven.
  // GOFR v1 blobs carry no verdict word — the caller cross-check
  // defaults to unproven, exactly like a fresh v1 reader.
  if ( cursor == word_count )
    return true;
  if ( cursor + 5 <= word_count && words[cursor] == kGoIrFrameTailMagic && words[cursor + 1] >= 1u
       && words[cursor + 1] <= kGoIrFrameTailVersion )
  {
    const std::uint32_t tail_version = words[cursor + 1];
    cursor += 2;
    out->frame_size = words[cursor++];
    out->param_window = words[cursor++];
    const std::size_t result_count = std::size_t(words[cursor++]);
    if ( result_count > kGoFrameMaxResults || result_count > (word_count - cursor) / 3 )
      return false;
    out->results.resize(result_count);
    for ( std::size_t i = 0; i < result_count; ++i )
    {
      out->results[i].opcode = words[cursor++];
      out->results[i].sp_offset = words[cursor++];
      out->results[i].store_offset = words[cursor++];
    }
    if ( tail_version >= 2u )
    {
      if ( cursor >= word_count )
        return false;
      out->results_caller_state = words[cursor++];
      if ( out->results_caller_state > kGoFunctionIrResultsStateMax )
        return false;
    }
  }

  // Optional G10 morestack stack-check prologue tail: absent when the
  // prologue was not proven (if_offset stays UINT32_MAX, exactly like a
  // fresh reader that never saw the tail).
  if ( cursor == word_count )
    return true;
  if ( cursor + 12 <= word_count && words[cursor] == kGoIrPrologueTailMagic
       && words[cursor + 1] == kGoIrPrologueTailVersion )
  {
    cursor += 2;
    out->prologue.sp_get_offset = words[cursor++];
    out->prologue.guard_load_offset = words[cursor++];
    out->prologue.guard_const_offset = words[cursor++];
    out->prologue.framesize_extra = words[cursor++];
    out->prologue.compare_offset = words[cursor++];
    out->prologue.if_offset = words[cursor++];
    out->prologue.store_offset = words[cursor++];
    out->prologue.call_offset = words[cursor++];
    out->prologue.br_if_offset = words[cursor++];
    out->prologue.end_offset = words[cursor++];
  }

  // Optional write-barrier site tail: absent when no site was proven
  // (wb_sites stays empty, exactly like a fresh reader that never saw it).
  if ( cursor == word_count )
    return true;
  if ( cursor + 3 <= word_count && words[cursor] == kGoIrWbTailMagic
       && words[cursor + 1] == kGoIrWbTailVersion )
  {
    cursor += 2;
    const std::size_t site_count = std::size_t(words[cursor++]);
    if ( site_count > kGoWbMaxSites || site_count > (word_count - cursor) / 6 )
      return false;
    out->wb_sites.resize(site_count);
    for ( std::size_t i = 0; i < site_count; ++i )
    {
      out->wb_sites[i].guard_if_offset = words[cursor++];
      out->wb_sites[i].call_offset = words[cursor++];
      out->wb_sites[i].arity = words[cursor++];
      out->wb_sites[i].buf_local = words[cursor++];
      out->wb_sites[i].spill_count = words[cursor++];
      const std::uint32_t flags = words[cursor++];
      if ( (flags & ~3u) != 0 )
        return false;
      out->wb_sites[i].has_in_body_nil_guard = (flags & 1u) != 0;
      out->wb_sites[i].has_old_value_chain = (flags & 2u) != 0;
    }
  }

  return cursor == word_count;
}

// Prove js.Value receiver globals by usage. See the
// ir.hpp contract. The walk is per-body: decode the instruction stream
// once, map every local that holds a constant-address 8-byte load
// (`i64.const K; i32.wrap_i64; i64.load a=3 off=0; local.set L`), then
// for each recorded call to a known-API js.Value method decode every
// recorded window store's operands — the value operand must be
// `local.get L`, the address operand either a bare `local.get SP`
// (window offset = the store's memarg) or the standard quintet
// `local.get SP; i64.extend_i32_u; i64.const K; i64.add; i32.wrap_i64`
// (window offset K) — and require the receiver pair (offsets 0 and 8) to
// reference locals loaded from adjacent constant addresses A, A+8.
bool go_jsvalue_receiver_globals(const module_t &module, std::vector<std::uint64_t> *out)
{
  if ( out == nullptr )
    return false;
  out->clear();
  if ( !module.go_provenance )
    return true; // fail-closed: no Go provenance, nothing proven
  std::vector<std::uint64_t> proven;
  for ( size_t b = 0; b < module.bodies.size(); ++b )
  {
    const body_t &body = module.bodies[b];
    const go_function_ir_t &ir = body.go_ir;
    if ( !ir.present || ir.calls.empty() )
      continue;
    bool any_js_callee = false;
    for ( const go_call_ir_t &call : ir.calls )
    {
      if ( call.fn_index != UINT32_MAX && go_known_api_jsvalue_callee(module, call.fn_index) )
      {
        any_js_callee = true;
        break;
      }
    }
    if ( !any_js_callee )
      continue;
    std::vector<binary::decoded_insn_t> insns;
    if ( !decode_go_body_insns(module, b, &insns) )
      continue;
    // insn offset -> index (the decoded stream is offset-ascending).
    std::unordered_map<std::uint32_t, size_t> by_offset;
    for ( size_t k = 0; k < insns.size(); ++k )
      by_offset.emplace(insns[k].offset, k);
    // local index -> constant load address. A local set from two
    // different constants is ambiguous and rejected at use.
    std::unordered_map<std::uint32_t, std::uint64_t> const_load_locals;
    std::unordered_set<std::uint32_t> ambiguous_locals;
    for ( size_t k = insns.size(); k >= 4; --k )
    {
      const size_t i = k - 4;             // [i64.const][wrap][i64.load][local.set]
      if ( insns[i].opcode != 0x42        // i64.const K
           || insns[i + 1].opcode != 0xA7 // i32.wrap_i64
           || insns[i + 2].opcode != 0x29 // i64.load
           || insns[i + 2].imm[0] != 3 || insns[i + 2].imm[1] != 0
           || insns[i + 3].opcode != 0x21 ) // local.set L
        continue;
      const std::uint32_t local_index = std::uint32_t(insns[i + 3].imm[0]);
      const std::uint64_t address = insns[i].imm[0];
      const auto existing = const_load_locals.find(local_index);
      if ( existing != const_load_locals.end() )
      {
        if ( existing->second != address )
          ambiguous_locals.insert(local_index);
        continue;
      }
      const_load_locals.emplace(local_index, address);
    }
    if ( const_load_locals.empty() )
      continue;
    // One recorded window store -> (window offset, value local) or reject.
    const auto decode_store = [&](const go_call_ir_t &call, const go_arg_ir_t &arg,
                                  std::uint32_t *window_offset, std::uint32_t *value_local) -> bool
    {
      if ( arg.opcode != 0x37 ) // i64.store only
        return false;
      const auto at = by_offset.find(arg.store_offset);
      if ( at == by_offset.end() || insns[at->second].opcode != 0x37 )
        return false;
      const size_t s = at->second;
      if ( s < 2 || insns[s - 1].opcode != 0x20 ) // value: local.get L
        return false;
      *value_local = std::uint32_t(insns[s - 1].imm[0]);
      if ( s >= 2 && insns[s - 2].opcode == 0x20
           && std::uint32_t(insns[s - 2].imm[0]) == call.sp_cache_local )
      {
        // Address form B: bare `local.get SP`; the window offset rides the
        // store's own memarg.
        *window_offset = std::uint32_t(insns[s].imm[1]);
        return true;
      }
      // Address form A: the quintet ending right before the value local.get.
      if ( s >= 6 && insns[s - 6].opcode == 0x20
           && std::uint32_t(insns[s - 6].imm[0]) == call.sp_cache_local
           && insns[s - 5].opcode == 0xAD   // i64.extend_i32_u
           && insns[s - 4].opcode == 0x42   // i64.const K
           && insns[s - 3].opcode == 0x7C   // i64.add
           && insns[s - 2].opcode == 0xA7 ) // i32.wrap_i64
      {
        *window_offset = std::uint32_t(insns[s - 4].imm[0]) + std::uint32_t(insns[s].imm[1]);
        return true;
      }
      return false;
    };
    for ( const go_call_ir_t &call : ir.calls )
    {
      if ( call.fn_index == UINT32_MAX || !go_known_api_jsvalue_callee(module, call.fn_index) )
        continue;
      const go_arg_ir_t *word0 = nullptr;
      const go_arg_ir_t *word1 = nullptr;
      std::uint32_t local0 = UINT32_MAX;
      std::uint32_t local1 = UINT32_MAX;
      for ( const go_arg_ir_t &arg : call.args )
      {
        std::uint32_t window_offset = 0;
        std::uint32_t value_local = 0;
        if ( !decode_store(call, arg, &window_offset, &value_local) )
          continue;
        if ( window_offset == 0 && word0 == nullptr )
        {
          word0 = &arg;
          local0 = value_local;
        }
        else if ( window_offset == 8 && word1 == nullptr )
        {
          word1 = &arg;
          local1 = value_local;
        }
      }
      if ( word0 == nullptr || word1 == nullptr )
        continue;
      if ( ambiguous_locals.count(local0) != 0 || ambiguous_locals.count(local1) != 0 )
        continue;
      const auto addr0 = const_load_locals.find(local0);
      const auto addr1 = const_load_locals.find(local1);
      if ( addr0 == const_load_locals.end() || addr1 == const_load_locals.end() )
        continue;
      if ( addr1->second != addr0->second + 8 )
        continue;
      proven.push_back(addr0->second);
    }
  }
  std::sort(proven.begin(), proven.end());
  proven.erase(std::unique(proven.begin(), proven.end()), proven.end());
  *out = std::move(proven);
  return true;
}

} // namespace wasmhex::loader
