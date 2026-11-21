#pragma once

#include <cstddef>
#include <cstdint>

// Wasmhex_c_frame_metadata_t and wasmhex_bounds_mode_t moved to
// core/types.hpp so the pure core can use them; re-exported here for the
// processor/decompiler modules.
#include "../../../core/types.hpp"

constexpr int wasmhex_user_regs = 64;
constexpr int wasmhex_local_kreg_budget = 3072;
constexpr int wasmhex_operand_kreg_budget = 3072;
constexpr int wasmhex_scratch_kreg_budget = 2048;
constexpr int wasmhex_cs_reg = wasmhex_user_regs;
constexpr int wasmhex_ds_reg = wasmhex_user_regs + 1;
constexpr int wasmhex_register_count = wasmhex_user_regs + 2;
constexpr uint64_t wasmhex_memory_base = 0;
// Map the full Memory32 range; runtime-size checks preserve growable bounds.
constexpr uint64_t wasmhex_max_growable_memory_pages = 65536;

// Stack frame layout inside the function's local-variable range [0, mba->frsize):
//
//   vd_stkoff = 0  +--------------------------------------------------+ <- 0
//                  | C shadow stack frame [0, c_frame_bytes)          |
//                  | (mem[fp + k] with 0 <= k < c_frame_bytes maps    |
//                  |  directly to vd_stkoff = k; empty when           |
//                  |  c_frame_bytes == 0)                             |
//                  +--------------------------------------------------+ <- wasmhex_overflow_locals_base(c_frame_bytes)
//                  |                                                  |    = align8(c_frame_bytes)
//                  | Overflow locals (index >= wasmhex_local_kreg_budget)
//                  | Each slot occupies 8 bytes (wasmhex_local_frame_slot_bytes):
//                  |   slot(idx) = align8(c_frame_bytes)              |
//                  |             + (idx - wasmhex_local_kreg_budget) * 8
//                  +--------------------------------------------------+ <- tuple_buffer_offset
//                  | Shared Tuple / SRET Call Result Buffer           |
//                  | Size >= 16 bytes, aligned to an 8-byte boundary  |
//                  | (placed at the top of the local frame so taking  |
//                  |  its mop_a address only lowers minstkref to      |
//                  |  tuple_buffer_offset and never aliases C shadow  |
//                  |  stack vars or overflow locals below it)         |
//   vd_stkoff =    +--------------------------------------------------+
//     mba->frsize
constexpr int wasmhex_local_frame_slot_bytes = 8;
constexpr int wasmhex_min_call_result_frame_bytes = 16;

inline constexpr std::size_t wasmhex_align_frame_size(std::size_t bytes)
{
  return (bytes + 7u) & ~std::size_t(7u);
}

inline constexpr std::size_t wasmhex_overflow_locals_base(std::size_t c_frame_bytes = 0)
{
  return wasmhex_align_frame_size(c_frame_bytes);
}

inline constexpr std::size_t wasmhex_overflow_local_count(std::size_t total_locals)
{
  return total_locals > std::size_t(wasmhex_local_kreg_budget)
    ? total_locals - std::size_t(wasmhex_local_kreg_budget)
    : 0;
}

inline constexpr std::size_t wasmhex_overflow_locals_frame_bytes(std::size_t total_locals,
                                                                 std::size_t c_frame_bytes = 0)
{
  return wasmhex_overflow_locals_base(c_frame_bytes)
    + wasmhex_overflow_local_count(total_locals) * std::size_t(wasmhex_local_frame_slot_bytes);
}

inline constexpr std::int64_t wasmhex_overflow_local_stkoff(std::size_t local_index,
                                                            std::size_t c_frame_bytes = 0)
{
  return local_index >= std::size_t(wasmhex_local_kreg_budget)
    ? std::int64_t(wasmhex_overflow_locals_base(c_frame_bytes)
                   + (local_index - std::size_t(wasmhex_local_kreg_budget))
                     * std::size_t(wasmhex_local_frame_slot_bytes))
    : -1;
}

inline constexpr std::size_t wasmhex_call_result_frame_bytes(std::size_t max_tuple_call_bytes)
{
  const std::size_t raw = max_tuple_call_bytes > std::size_t(wasmhex_min_call_result_frame_bytes)
    ? max_tuple_call_bytes
    : std::size_t(wasmhex_min_call_result_frame_bytes);
  return wasmhex_align_frame_size(raw);
}

inline constexpr std::size_t wasmhex_calc_frame_size(std::size_t total_locals,
                                                     std::size_t max_tuple_call_bytes,
                                                     std::size_t c_frame_bytes = 0)
{
  return wasmhex_overflow_locals_frame_bytes(total_locals, c_frame_bytes)
    + wasmhex_call_result_frame_bytes(max_tuple_call_bytes);
}
