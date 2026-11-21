// Implementation of the constant-address evidence
// walk — see mem_drefs.hpp for the contract and the fail-closed policy.
#include "mem_drefs.hpp"

#include "../binary/instruction_stream.hpp"

#include <algorithm>
#include <array>
#include <optional>

namespace wasmhex::loader
{
namespace
{

// A stack cell: a proven constant (the full i64 value; i32 ops wrap at use)
// or unknown. `unknown` taints every consumer; the walk never guesses.
using cell_t = std::optional<std::uint64_t>;

// Deepest modeled stack: wasm valid code is bounded well below this; a
// stream that tries to push deeper fails closed (stack reset) instead of
// allocating unbounded memory on malformed input.
constexpr std::size_t max_stack_depth = 256;

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
  // Everything that changes or branches the flow — plus every opcode whose
  // arity is not modeled below. Clearing is the fail-closed default.
  return (opcode >= 0x02 && opcode <= 0x0F) || opcode == 0x11 || opcode == 0x1C || opcode == 0x1D
    || opcode >= 0xFB;
}

struct stack_t
{
  std::vector<cell_t> cells;

  bool push(cell_t cell)
  {
    if ( cells.size() >= max_stack_depth )
      return false;
    cells.push_back(cell);
    return true;
  }

  cell_t pop()
  {
    if ( cells.empty() )
      return cell_t(); // empty = unknown; popping an empty abstract stack
                       // happens after unmodeled joins — stay conservative
    const cell_t top = cells.back();
    cells.pop_back();
    return top;
  }

  void clear() { cells.clear(); }
};

void walk_body(const module_t &module, std::size_t body_index, std::vector<mem_dref_t> *out_mem,
               std::vector<global_ref_t> *out_globals)
{
  const body_t &body = module.bodies[body_index];
  if ( body.offset >= module.code.size() || body.size == 0 )
    return;
  const uchar *payload = module.code.data() + body.offset;
  const std::size_t avail
    = std::min<std::size_t>(body.size, module.code.size() - std::size_t(body.offset));

  // module.code holds the REWRITTEN opcode stream — parse_code copies the
  // body from after its local declarations (body.pos), so body.offset
  // points at the first opcode and no local-decl skip is needed here.
  binary::insn_cursor_t cursor{ payload, avail, 0, nullptr };
  binary::decoded_insn_t insn;
  stack_t stack;
  while ( cursor.pos < cursor.size && binary::next_insn(cursor, &insn) )
  {
    const std::uint32_t insn_offset = insn.offset;
    switch ( insn.opcode )
    {
      case 0x41: // i32.const
      case 0x42: // i64.const
        if ( !stack.push(cell_t(insn.imm[0])) )
          stack.clear();
        break;
      case 0x20: // local.get — unknown value
        if ( !stack.push(cell_t()) )
          stack.clear();
        break;
      case 0x21: // local.set — consumes one operand
        stack.pop();
        break;
      case 0x22: // local.tee — consumes and re-pushes one operand
      {
        const cell_t value = stack.pop();
        if ( !stack.push(value) )
          stack.clear();
        break;
      }
      case 0x23: // global.get — proven reference by opcode; unknown value
      {
        global_ref_t record;
        record.body_index = std::uint32_t(body_index);
        record.insn_offset = insn_offset;
        record.global_index = std::uint32_t(insn.imm[0]);
        out_globals->push_back(record);
        if ( !stack.push(cell_t()) )
          stack.clear();
        break;
      }
      case 0x6A: // i32.add
      case 0x6B: // i32.sub
      case 0x7C: // i64.add
      case 0x7D: // i64.sub
      {
        const cell_t rhs = stack.pop();
        const cell_t lhs = stack.pop();
        if ( lhs.has_value() && rhs.has_value() )
        {
          const std::uint64_t sum = insn.opcode == 0x6B || insn.opcode == 0x7D
            ? uint64(std::int64_t(*lhs) - std::int64_t(*rhs))
            : *lhs + *rhs;
          const std::uint64_t wrapped
            = (insn.opcode == 0x6A || insn.opcode == 0x6B) ? uint32(sum) : sum;
          if ( !stack.push(cell_t(wrapped)) )
            stack.clear();
        }
        else if ( !stack.push(cell_t()) )
          stack.clear();
        break;
      }
      case 0xA7: // i32.wrap_i64
      {
        const cell_t value = stack.pop();
        const std::uint64_t wrapped = value.has_value() ? uint32(*value) : 0;
        if ( !stack.push(value.has_value() ? cell_t(wrapped) : cell_t()) )
          stack.clear();
        break;
      }
      case 0xAD: // i64.extend_i32_u
      {
        const cell_t value = stack.pop();
        if ( !stack.push(value) )
          stack.clear();
        break;
      }
      default:
        if ( is_scalar_load(insn.opcode) )
        {
          const cell_t addr = stack.pop();
          if ( addr.has_value() )
          {
            mem_dref_t record;
            record.body_index = std::uint32_t(body_index);
            record.insn_offset = insn_offset;
            record.address = *addr + insn.imm[1]; // effective = operand + memarg offset
            record.store = false;
            out_mem->push_back(record);
          }
          if ( !stack.push(cell_t()) ) // the loaded value is unknown (mutable memory)
            stack.clear();
        }
        else if ( is_scalar_store(insn.opcode) )
        {
          stack.pop(); // value
          const cell_t addr = stack.pop();
          if ( addr.has_value() )
          {
            mem_dref_t record;
            record.body_index = std::uint32_t(body_index);
            record.insn_offset = insn_offset;
            record.address = *addr + insn.imm[1];
            record.store = true;
            out_mem->push_back(record);
          }
        }
        else if ( insn.opcode == 0x1A ) // drop
        {
          stack.pop();
        }
        else if ( insn.opcode == 0x1B || insn.opcode == 0x1C ) // select (typed or not)
        {
          stack.pop();
          stack.pop();
          stack.pop();
          if ( !stack.push(cell_t()) )
            stack.clear();
        }
        else if ( is_control_clear(insn.opcode) || insn.opcode == 0x10 /* call */
                  || insn.opcode == 0x0D /* br_if: pops the condition */
        )
        {
          if ( insn.opcode == 0x0D || insn.opcode == 0x0E || insn.opcode == 0x04 /* if */ )
            stack.pop(); // condition / selector — then clear
          stack.clear();
        }
        else
        {
          // Unmodeled plain opcode: its arity is unknown — fail closed.
          stack.clear();
        }
        break;
    }
  }
}

} // namespace

std::size_t const_mem_drefs(const module_t &module, std::vector<mem_dref_t> *out_mem,
                            std::vector<global_ref_t> *out_globals)
{
  if ( out_mem == nullptr || out_globals == nullptr )
    return 0;
  out_mem->clear();
  out_globals->clear();
  std::size_t scanned = 0;
  for ( std::size_t i = 0; i < module.bodies.size(); ++i )
  {
    walk_body(module, i, out_mem, out_globals);
    ++scanned;
  }
  return scanned;
}

} // namespace wasmhex::loader
