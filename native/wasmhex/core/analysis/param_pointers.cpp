// C string args: implementation of the callee-side
// pointer-parameter evidence walk — see param_pointers.hpp for the contract
// and the fail-closed policy.
#include "param_pointers.hpp"

#include "../binary/instruction_stream.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <utility>

namespace wasmhex::loader
{
namespace
{

// Known libc pointer positions. printf/scanf family: the format position is
// a pointer (rendered as a string literal when the bytes prove one); buffer
// positions are pointers; sizes/lengths never are. MD5 & friends are NOT
// here on purpose — the body walk proves their context pointers.
const libc_pointer_api_t kLibcPointerApis[] = {
  { "printf", 0x1 },    // (const char *format, ...)
  { "fprintf", 0x5 },   // (FILE *, const char *format, ...)
  { "sprintf", 0x3 },   // (char *str, const char *format, ...)
  { "snprintf", 0x5 },  // (char *str, size_t, const char *format, ...)
  { "vprintf", 0x1 },   // (const char *format, va_list)
  { "vfprintf", 0x5 },  // (FILE *, const char *format, va_list)
  { "vsprintf", 0x3 },  // (char *str, const char *format, va_list)
  { "vsnprintf", 0x5 }, // (char *str, size_t, const char *format, ...)
  { "scanf", 0x1 },     // (const char *format, ...)
  { "fscanf", 0x3 },    // (FILE *, const char *format, ...)
  { "sscanf", 0x3 },    // (const char *str, const char *format, ...)
  { "vscanf", 0x1 },    // (const char *format, va_list)
  { "vsscanf", 0x3 },   // (const char *str, const char *format, va_list)
  { "vfscanf", 0x3 },   // (FILE *, const char *format, va_list)
  { "puts", 0x1 },      // (const char *s)
  { "fputs", 0x3 },     // (const char *s, FILE *)
  { "strlen", 0x1 },    // (const char *s)
  { "strcpy", 0x3 },    // (char *dest, const char *src)
  { "strncpy", 0x7 },   // (char *dest, const char *src, size_t)
  { "strcmp", 0x3 },    // (const char *s1, const char *s2)
  { "strncmp", 0x7 },   // (const char *, const char *, size_t)
  { "strcat", 0x3 },    // (char *dest, const char *src)
  { "strncat", 0x7 },   // (char *, const char *, size_t)
  { "strchr", 0x1 },    // (const char *s, int)
  { "strrchr", 0x1 },   // (const char *s, int)
  { "strstr", 0x3 },    // (const char *haystack, const char *needle)
  { "memcpy", 0x3 },    // (void *dest, const void *src, size_t)
  { "memmove", 0x3 },   // (void *dest, const void *src, size_t)
  { "memset", 0x1 },    // (void *s, int, size_t)
  { "memcmp", 0x3 },    // (const void *, const void *, size_t)
};

// Deepest modeled operand stack (same policy as mem_drefs: fail closed).
constexpr std::size_t max_stack_depth = 256;

// Fixpoint pass cap. Masks only grow, so more passes can only add proofs;
// the cap trades completeness for bounded load time on huge modules and
// keeps the under-converged result safe (fewer positions proven).
constexpr int max_fixpoint_passes = 4;

bool is_scalar_load(uchar opcode)
{
  return opcode >= 0x28 && opcode <= 0x35;
}

bool is_scalar_store(uchar opcode)
{
  return opcode >= 0x36 && opcode <= 0x3E;
}

bool is_control_clear(uchar opcode)
{
  return (opcode >= 0x02 && opcode <= 0x0F) || opcode == 0x11 || opcode == 0x1C || opcode == 0x1D
    || opcode >= 0xFB;
}

// An abstract value: which parameters flow into it (bitmask), a proven
// constant when one is available, and — for shadow-stack spill tracking —
// the `sp-like base local + constant offset` form. Compilers spill C
// parameters through one sp-derived local (e.g. `global.get $sp;
// i32.const 16; i32.sub; local.set $fp`, with or without a loader-proven
// C frame); keeping the (base local, offset) identity lets a spilled
// parameter mask survive the store/load round-trip. Cells only exist for
// sp-like bases: two different base locals are only aliased when they hold
// the same sp value, in which case the may-union below still unions real
// writes to the same slot.
struct pp_cell_t
{
  std::uint64_t mask = 0;
  bool has_const = false;
  std::uint64_t konst = 0;
  bool sp_like = false;         // derives from a global.get (kept only while
                                // EVERY def stays sp-like — AND-merge)
  std::int32_t base_local = -1; // bare base local (no arithmetic applied)
  std::int32_t base_off = 0;    // constant delta applied to the base local
};

struct pp_state_t
{
  const module_t &module;
  const std::vector<std::uint64_t> &table_masks;
  const std::vector<std::uint64_t> &proven; // running fixpoint (function idx)
  const std::uint64_t memory_limit;
  const bool record; // final pass: collect constant pointer-arg records
  std::vector<pointer_arg_record_t> *const records;
  const std::uint32_t import_count;

  pp_state_t(const module_t &m, const std::vector<std::uint64_t> &tables,
             const std::vector<std::uint64_t> &prov, std::uint64_t limit, bool rec,
             std::vector<pointer_arg_record_t> *out)
      : module(m), table_masks(tables), proven(prov), memory_limit(limit), record(rec),
        records(out), import_count(uint32(m.imported_func_types.size()))
  {
  }

  static pp_cell_t merge(const pp_cell_t &a, const pp_cell_t &b)
  {
    pp_cell_t out;
    out.mask = a.mask | b.mask;
    out.has_const = a.has_const && b.has_const && a.konst == b.konst;
    out.konst = out.has_const ? a.konst : 0;
    out.sp_like = a.sp_like && b.sp_like;
    // Base identity survives only when both defs name the same base+offset.
    out.base_local = a.base_local == b.base_local ? a.base_local : -1;
    out.base_off = out.base_local >= 0 && a.base_off == b.base_off ? a.base_off : 0;
    return out;
  }

  void run(std::size_t body_index, std::uint64_t *body_mask)
  {
    const body_t &body = module.bodies[body_index];
    if ( body.offset >= module.code.size() || body.size == 0 )
      return;
    const uchar *payload = module.code.data() + body.offset;
    const std::size_t avail
      = std::min<std::size_t>(body.size, module.code.size() - std::size_t(body.offset));
    // module.code is the rewritten opcode stream (same as mem_drefs).
    binary::insn_cursor_t cursor{ payload, avail, 0, nullptr };
    binary::decoded_insn_t insn;

    const wasm_type_t &func_sig = module.types[module.defined_func_types[body_index]];
    const std::size_t param_count = func_sig.params.size();
    const std::size_t local_count = param_count + body.local_types.size();
    // Locals are function-scoped: they survive control clears. Facts are a
    // union over defs in linear order (may-analysis) — a marked parameter
    // always has a real def chain to the instruction that used it. sp-like
    // and base identity use AND/equality so they can never be faked by a
    // conflicting def.
    std::vector<pp_cell_t> locals(local_count);
    // Parameters are entry-defined; declared locals start wasm-zero but the
    // walk treats them as undefined so the FIRST def anchors facts (merging
    // a def with the zero-init cell would AND away sp-likeness).
    std::vector<bool> local_defined(local_count, false);
    for ( std::size_t p = 0; p < param_count && p < 64; ++p )
    {
      locals[p].mask = std::uint64_t(1) << p;
      local_defined[p] = true;
    }
    const auto assign_local = [&](std::size_t index, const pp_cell_t &value)
    {
      locals[index] = local_defined[index] ? merge(locals[index], value) : value;
      local_defined[index] = true;
    };
    // Spill cells keyed by (sp-like base local, byte offset).
    std::map<std::pair<std::int32_t, std::int32_t>, pp_cell_t> cells;
    const auto kill_cells = [&](std::int32_t local_index)
    {
      if ( local_index < 0 )
        return;
      for ( auto it = cells.begin(); it != cells.end(); )
      {
        if ( it->first.first == local_index )
          it = cells.erase(it);
        else
          ++it;
      }
    };

    std::vector<pp_cell_t> stack;
    const auto push = [&](const pp_cell_t &cell) -> bool
    {
      if ( stack.size() >= max_stack_depth )
        return false;
      stack.push_back(cell);
      return true;
    };
    const auto pop = [&]() -> pp_cell_t
    {
      if ( stack.empty() )
        return pp_cell_t{};
      const pp_cell_t top = stack.back();
      stack.pop_back();
      return top;
    };

    while ( cursor.pos < cursor.size && binary::next_insn(cursor, &insn) )
    {
      switch ( insn.opcode )
      {
        case 0x41: // i32.const
        case 0x42: // i64.const
        {
          pp_cell_t cell;
          cell.has_const = true;
          cell.konst = insn.imm[0];
          if ( !push(cell) )
            stack.clear();
          break;
        }
        case 0x20: // local.get
        {
          if ( !push(size_t(insn.imm[0]) < local_count ? locals[size_t(insn.imm[0])]
                                                       : pp_cell_t{}) )
            stack.clear();
          break;
        }
        case 0x21: // local.set — union with prior defs; the spill cells
          // keyed by THIS local die with its old value
          {
            const pp_cell_t value = pop();
            if ( size_t(insn.imm[0]) < local_count )
            {
              const std::int32_t index = std::int32_t(insn.imm[0]);
              kill_cells(index);
              assign_local(size_t(index), value);
              // An sp-like local becomes its own addressing anchor: later
              // [local +/- k] accesses resolve to spill cells keyed by it.
              if ( locals[size_t(index)].sp_like )
              {
                locals[size_t(index)].base_local = index;
                locals[size_t(index)].base_off = 0;
              }
            }
            break;
          }
        case 0x22: // local.tee
        {
          const pp_cell_t value = pop();
          if ( size_t(insn.imm[0]) < local_count )
          {
            const std::int32_t index = std::int32_t(insn.imm[0]);
            kill_cells(index);
            assign_local(size_t(index), value);
            if ( locals[size_t(index)].sp_like )
            {
              locals[size_t(index)].base_local = index;
              locals[size_t(index)].base_off = 0;
            }
            if ( !push(locals[size_t(index)]) )
              stack.clear();
          }
          else if ( !push(pp_cell_t{}) )
            stack.clear();
          break;
        }
        case 0x23: // global.get — unknown value; sp-like base candidate
        {
          pp_cell_t cell;
          cell.sp_like = true;
          if ( !push(cell) )
            stack.clear();
          break;
        }
        case 0x24: // global.set consumes one operand
        {
          pop();
          break;
        }
        case 0x6A: // i32.add
        case 0x6B: // i32.sub
        case 0x7C: // i64.add
        case 0x7D: // i64.sub
        {
          const pp_cell_t rhs = pop();
          const pp_cell_t lhs = pop();
          pp_cell_t out;
          out.mask = lhs.mask | rhs.mask;
          if ( lhs.has_const && rhs.has_const )
          {
            out.has_const = true;
            out.konst = insn.opcode == 0x6B || insn.opcode == 0x7D
              ? uint64(std::int64_t(lhs.konst) - std::int64_t(rhs.konst))
              : lhs.konst + rhs.konst;
            if ( insn.opcode == 0x6A || insn.opcode == 0x6B )
              out.konst = uint32(out.konst);
          }
          // The sp-like identity survives +/- constant offsets only (a raw
          // global.get carries base_local -1 until a local.set anchors it).
          if ( lhs.sp_like && !rhs.sp_like && rhs.has_const )
          {
            out.sp_like = true;
            out.base_local = lhs.base_local;
            out.base_off = lhs.base_off
              + (insn.opcode == 0x6B || insn.opcode == 0x7D ? -std::int32_t(uint32(rhs.konst))
                                                            : std::int32_t(uint32(rhs.konst)));
          }
          if ( !push(out) )
            stack.clear();
          break;
        }
        case 0xA7: // i32.wrap_i64
        case 0xAC: // i64.extend_i32_s
        case 0xAD: // i64.extend_i32_u
        {
          const pp_cell_t value = pop();
          pp_cell_t out = value;
          if ( out.has_const && (insn.opcode == 0xA7 || insn.opcode == 0xAC) )
            out.konst = uint32(out.konst);
          if ( !push(out) )
            stack.clear();
          break;
        }
        case 0x71: // i32.and — a masked pointer is still a pointer
        case 0x72: // i32.or
        case 0x73: // i32.xor
        {
          const pp_cell_t rhs = pop();
          const pp_cell_t lhs = pop();
          pp_cell_t out;
          out.mask = lhs.mask | rhs.mask;
          if ( !push(out) )
            stack.clear();
          break;
        }
        case 0x10: // call
        {
          const std::uint32_t callee = uint32(insn.imm[0]);
          std::size_t arity = 0;
          std::size_t result_count = 1;
          if ( callee < import_count )
          {
            const wasm_type_t &signature = module.types[module.imported_func_types[callee]];
            arity = signature.params.size();
            result_count = signature.results.size();
          }
          else if ( size_t(callee - import_count) < module.bodies.size() )
          {
            const wasm_type_t &signature
              = module.types[module.defined_func_types[size_t(callee - import_count)]];
            arity = signature.params.size();
            result_count = signature.results.size();
          }
          else
          {
            stack.clear();
            break;
          }
          if ( stack.size() < arity )
          {
            stack.clear();
            break;
          }
          std::uint64_t effective = 0;
          if ( size_t(callee) < table_masks.size() )
            effective |= table_masks[size_t(callee)];
          if ( callee >= import_count && size_t(callee - import_count) < proven.size() )
            effective |= proven[size_t(callee - import_count)];
          // Args pop in reverse (LIFO); argument j is stack[base + j].
          const std::size_t base = stack.size() - arity;
          for ( std::size_t j = 0; j < arity; ++j )
          {
            const pp_cell_t &arg = stack[base + j];
            if ( j < 64 && (effective & (std::uint64_t(1) << j)) != 0 )
            {
              *body_mask |= arg.mask; // transitive: params feeding a pointer arg
              if ( record && memory_limit != 0 && arg.has_const && uint32(arg.konst) != 0
                   && uint64(uint32(arg.konst)) < memory_limit )
              {
                pointer_arg_record_t rec;
                rec.body_index = std::uint32_t(body_index);
                rec.insn_offset = insn.offset;
                rec.param_index = std::uint32_t(j);
                rec.address = uint64(uint32(arg.konst));
                records->push_back(rec);
              }
            }
          }
          stack.resize(base);
          // Multi-result calls push one cell per result (contents unknown);
          // void calls push nothing — a phantom would misalign every later
          // call's argument window.
          for ( std::size_t r = 0; r < result_count; ++r )
            if ( !push(pp_cell_t{}) )
            {
              stack.clear();
              break;
            }
          break;
        }
        default:
          if ( is_scalar_load(insn.opcode) )
          {
            const pp_cell_t addr = pop();
            const bool spill_base = addr.sp_like && addr.base_local >= 0;
            const std::int64_t cell_off = std::int64_t(addr.base_off) + std::int64_t(insn.imm[1]);
            if ( spill_base && cell_off >= INT32_MIN && cell_off <= INT32_MAX )
            {
              const auto cell = cells.find(std::make_pair(addr.base_local, std::int32_t(cell_off)));
              if ( !push(cell != cells.end() ? cell->second : pp_cell_t{}) )
                stack.clear();
            }
            else
            {
              *body_mask |= addr.mask; // proven: address use of those params
              if ( !push(pp_cell_t{}) )
                stack.clear();
            }
          }
          else if ( is_scalar_store(insn.opcode) )
          {
            const pp_cell_t value = pop(); // [address, value] — value on top
            const pp_cell_t addr = pop();
            const bool spill_base = addr.sp_like && addr.base_local >= 0;
            const std::int64_t cell_off = std::int64_t(addr.base_off) + std::int64_t(insn.imm[1]);
            if ( spill_base && cell_off >= INT32_MIN && cell_off <= INT32_MAX )
            {
              const std::pair<std::int32_t, std::int32_t> key(addr.base_local,
                                                              std::int32_t(cell_off));
              const auto cell = cells.find(key);
              if ( cell == cells.end() )
                cells.emplace(key, value);
              else
                cell->second = merge(cell->second, value);
            }
            else
            {
              *body_mask |= addr.mask; // proven: address use of those params
            }
          }
          else if ( insn.opcode == 0x1A ) // drop
          {
            pop();
          }
          else if ( insn.opcode == 0x1B || insn.opcode == 0x1C ) // select
          {
            pop();
            const pp_cell_t b = pop();
            const pp_cell_t a = pop();
            if ( !push(merge(a, b)) )
              stack.clear();
          }
          else if ( is_control_clear(insn.opcode) || insn.opcode == 0x0D )
          {
            if ( insn.opcode == 0x0D || insn.opcode == 0x0E || insn.opcode == 0x04 )
              pop(); // condition / selector — then clear
            stack.clear();
          }
          else
          {
            stack.clear(); // unmodeled arity: fail closed
          }
          break;
      }
    }
  }
};

} // namespace

const libc_pointer_api_t *find_libc_pointer_api(const std::string &name)
{
  for ( const libc_pointer_api_t &known : kLibcPointerApis )
    if ( name == known.name )
      return &known;
  return nullptr;
}

std::uint64_t libc_pointer_mask(const std::string &name)
{
  const libc_pointer_api_t *known = find_libc_pointer_api(name);
  return known != nullptr ? known->pointer_mask : 0;
}

param_pointer_result_t param_pointer_walk(const module_t &module,
                                          const std::vector<std::uint64_t> &table_masks,
                                          std::uint64_t memory_limit)
{
  param_pointer_result_t result;
  result.proven_param_masks.assign(module.bodies.size(), 0);
  // Passes until the masks stabilize (capped); one final pass records the
  // constant pointer arguments with the converged masks.
  for ( int pass = 0; pass < max_fixpoint_passes; ++pass )
  {
    std::vector<std::uint64_t> next = result.proven_param_masks;
    pp_state_t state(module, table_masks, next, memory_limit, false, nullptr);
    for ( std::size_t i = 0; i < module.bodies.size(); ++i )
      state.run(i, &next[i]);
    if ( next == result.proven_param_masks )
      break;
    result.proven_param_masks = std::move(next);
  }
  pp_state_t final_state(module, table_masks, result.proven_param_masks, memory_limit, true,
                         &result.arg_records);
  for ( std::size_t i = 0; i < module.bodies.size(); ++i )
  {
    std::uint64_t sink = 0;
    final_state.run(i, &sink);
  }
  return result;
}

} // namespace wasmhex::loader
