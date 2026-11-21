#include "function_validator.hpp"
#include "families/simd_validator.hpp"

#include <algorithm>

namespace wasmhex::loader
{
namespace
{

// Internal operand types. `unknown` is the bottom type produced by stack
// polymorphism after unreachable/br/return; it matches every type.
enum class vt : uchar
{
  unknown,
  i32,
  i64,
  f32,
  f64,
  v128,
  funcref,
  externref,
  ref, // any other reference (GC / typed); matched loosely
};

bool is_ref(vt type)
{
  return type == vt::funcref || type == vt::externref || type == vt::ref;
}

bool matches(vt actual, vt expected)
{
  if ( actual == expected || actual == vt::unknown || expected == vt::unknown )
    return true;
  // Loose reference subtyping: only the two disjoint MVP hierarchies are
  // distinguished; everything else is accepted.
  if ( is_ref(actual) && is_ref(expected) )
    return actual == vt::ref || expected == vt::ref;
  return false;
}

// Canonical bytes come from wasmhex_vt::decode_value_type().
vt from_canonical(uchar type)
{
  switch ( type )
  {
    case 0x7F: return vt::i32;
    case 0x7E: return vt::i64;
    case 0x7D: return vt::f32;
    case 0x7C: return vt::f64;
    case 0x7B: return vt::v128;
    case 0x70: return vt::funcref;
    case 0x6F: return vt::externref;
    default: return vt::ref;
  }
}

bool is_value_type_byte(uchar byte)
{
  return (byte >= 0x7B && byte <= 0x7F) // numeric + v128
    || (byte >= 0x69 && byte <= 0x74)   // abstract reference shorthands
    || byte == 0x63 || byte == 0x64;    // (ref null ht) / (ref ht)
}

struct frame_t
{
  uchar opcode = 0; // 0x02 block, 0x03 loop, 0x04 if, 0x05 else, 0 = function
  std::vector<vt> start_types;
  std::vector<vt> end_types;
  size_t height = 0;
  size_t init_height = 0; // Legacy bookkeeping (kept for reference)
  // Locals this frame transitioned to initialized (the undo log
  // pop_ctrl clears exactly these bits, so assignments inside a branch
  // never survive its end)
  std::vector<uint32> frame_inits;
  bool unreachable = false;
};

class validator_t
{
public:
  validator_t(const module_t &module, const wasm_type_t &type,
              const std::vector<uchar> &declared_locals, const uchar *body, size_t size,
              const std::vector<uchar> *declared_ref_forms)
      : m(module), r{ body, size, 0 }
  {
    for ( uchar param : type.params )
      local_canonical.push_back(param);
    for ( uchar local : declared_locals )
      local_canonical.push_back(local);
    for ( size_t i = 0; i < local_canonical.size(); ++i )
    {
      const uchar form = i < type.params.size()
        ? type.param_ref_forms[i]
        : (declared_ref_forms != nullptr ? (*declared_ref_forms)[i - type.params.size()]
                                         : uchar(0));
      local_ref_forms.push_back(form);
    }
    // A non-defaultable local — a non-nullable reference, i.e.
    // ref_kind_abs from the decoder — must be definitely assigned before
    // any local.get. Initialization is tracked with the spec algorithm
    // (frame init_height + truncation at end), no longer a flat
    // set-anywhere list.
    for ( size_t i = 0; i < local_canonical.size(); ++i )
      local_non_defaultable.push_back(local_ref_forms[i] == uchar(wasmhex_vt::ref_kind_abs));
    for ( uchar param : type.params )
      locals.push_back(from_canonical(param));
    for ( uchar local : declared_locals )
      locals.push_back(from_canonical(local));
    for ( uchar result : type.results )
      results.push_back(from_canonical(result));
    // One initialized bit per local (params and defaultable locals
    // start initialized; the gate only reads the non-defaultable ones)
    local_initialized.assign(locals.size(), true);
    for ( size_t i = 0; i < locals.size(); ++i )
      if ( local_non_defaultable[i] )
        local_initialized[i] = false;
  }

  function_validation_t run()
  {
    frame_t function_frame;
    function_frame.end_types = results;
    ctrls.push_back(function_frame);
    while ( result.valid && result.complete )
    {
      if ( ctrls.empty() )
      {
        if ( r.pos != r.size )
          fail("WebAssembly function body has instructions after its final end");
        break;
      }
      if ( r.pos >= r.size )
      {
        fail("WebAssembly function body is missing its final end");
        break;
      }
      start = r.pos;
      uchar opcode = 0;
      r.byte(&opcode);
      result.opcode = opcode;
      step(opcode);
    }
    return result;
  }

private:
  const module_t &m;
  reader_t r;
  std::vector<vt> locals;
  std::vector<uchar> local_canonical;
  std::vector<uchar> local_ref_forms; // Typed locals carry a ref_kind
  std::vector<bool> local_non_defaultable;
  // Spec definite-assignment algorithm — an O(1) initialized-bit
  // per local plus a per-frame undo log. set/tee flips a false bit and
  // records the index on the CURRENT frame; pop_ctrl (and the else swap)
  // clears exactly those bits, so an assignment made inside a branch or
  // loop body never survives the enclosing end (the branch may not have
  // executed). The old ordered-list form was O(n) per local.get/set/tee.
  std::vector<bool> local_initialized;
  std::vector<uint32> local_inits; // legacy mirror kept empty
  std::vector<vt> results;
  std::vector<vt> vals;
  std::vector<frame_t> ctrls;
  function_validation_t result;
  size_t start = 0;

  // reporting

  void set_state(function_validation_state_t next, const char *reason, error_code code)
  {
    if ( result.state != function_validation_state_t::valid )
      return; // first verdict wins
    result.state = next;
    result.valid = next == function_validation_state_t::valid
      || next == function_validation_state_t::unsupported;
    result.complete = next != function_validation_state_t::unsupported;
    result.offset = start;
    result.reason = reason;
    result.code = code;
  }

  void fail(const char *reason, error_code code = error_code::bad_instruction)
  {
    set_state(function_validation_state_t::invalid, reason, code);
  }

  void malformed(const char *reason, error_code code = error_code::malformed_immediate)
  {
    set_state(function_validation_state_t::malformed, reason, code);
  }

  void stop(const char *reason, error_code code = error_code::policy_other)
  {
    set_state(function_validation_state_t::unsupported, reason, code);
  }

  bool ok() const { return result.valid && result.complete; }

  // immediates

  bool u32(uint64 *value)
  {
    if ( r.uleb(value, 32) )
      return true;
    malformed("WebAssembly instruction has a malformed immediate");
    return false;
  }

  bool index(uint64 *value, uint64 limit, const char *reason)
  {
    if ( !u32(value) )
      return false;
    if ( *value < limit )
      return true;
    fail(reason);
    return false;
  }

  // operand stack

  // The loose vt::ref bucket (every typed reference — (ref null ht)
  // (ref ht) and the GC abstract shorthands) used to flow through as a
  // plain value, letting the validator report Valid over types it never
  // really checked. Real subtyping needs the supertype closure from
  // sub/rec type declarations, and the front end does not parse those
  // (wasm_type_t carries no supertypes), so an honest validator stops
  // here: any function that moves a typed reference through its operand
  // stack is reported unsupported, not valid.
  void stop_typed_ref()
  {
    stop("typed reference types are not modeled by the validator", error_code::policy_gc_types);
  }

  void push(vt type)
  {
    if ( type == vt::ref )
      stop_typed_ref();
    vals.push_back(type);
  }

  void push(const std::vector<vt> &types)
  {
    for ( vt type : types )
      push(type);
  }

  vt pop()
  {
    frame_t &frame = ctrls.back();
    if ( vals.size() == frame.height )
    {
      if ( frame.unreachable )
        return vt::unknown;
      fail("WebAssembly operand stack underflow");
      return vt::unknown;
    }
    const vt top = vals.back();
    vals.pop_back();
    return top;
  }

  vt pop(vt expected)
  {
    if ( expected == vt::ref )
      stop_typed_ref();
    const vt actual = pop();
    if ( !matches(actual, expected) )
      fail("WebAssembly operand has the wrong type");
    return actual == vt::unknown ? expected : actual;
  }

  void pop(const std::vector<vt> &types)
  {
    for ( size_t i = types.size(); i-- > 0; )
      pop(types[i]);
  }

  void push_ctrl(uchar opcode, std::vector<vt> in, std::vector<vt> out)
  {
    frame_t frame;
    frame.opcode = opcode;
    frame.start_types = std::move(in);
    frame.end_types = std::move(out);
    frame.height = vals.size();
    frame.init_height = local_inits.size(); // Legacy
    frame.frame_inits.clear();              // Fresh undo log per frame
    push(frame.start_types);
    ctrls.push_back(std::move(frame));
  }

  bool pop_ctrl(frame_t *out)
  {
    if ( ctrls.empty() )
    {
      fail("WebAssembly control stack underflow");
      return false;
    }
    frame_t frame = ctrls.back();
    pop(frame.end_types);
    if ( vals.size() != ctrls.back().height )
      fail("WebAssembly block leaves extra values on the operand stack");
    vals.resize(std::min(vals.size(), ctrls.back().height));
    // Assignments made inside the frame do not survive its end
    // the branch may not have executed. The else case pops and re-pushes
    // the frame, so each branch starts from the frame-entry state.
    for ( uint32 idx : frame.frame_inits )
      local_initialized[idx] = false;
    ctrls.pop_back();
    *out = std::move(frame);
    return ok();
  }

  static const std::vector<vt> &label_types(const frame_t &frame)
  {
    return frame.opcode == 0x03 ? frame.start_types : frame.end_types;
  }

  void set_unreachable()
  {
    vals.resize(ctrls.back().height);
    ctrls.back().unreachable = true;
  }

  // module context

  size_t function_count() const
  {
    return m.imported_func_types.size() + m.defined_func_types.size();
  }

  const wasm_type_t *function_signature(uint64 function_index) const
  {
    const size_t imports = m.imported_func_types.size();
    const uint32 type_index = function_index < imports
      ? m.imported_func_types[size_t(function_index)]
      : m.defined_func_types[size_t(function_index - imports)];
    return type_index < m.types.size() ? &m.types[type_index] : nullptr;
  }

  static std::vector<vt> convert(const std::vector<uchar> &types)
  {
    std::vector<vt> out;
    out.reserve(types.size());
    for ( uchar type : types )
      out.push_back(from_canonical(type));
    return out;
  }

  vt memory_address_type() const { return m.memory64 ? vt::i64 : vt::i32; }

  vt table_address_type(uint64 table) const
  {
    return table < m.table64.size() && m.table64[size_t(table)] ? vt::i64 : vt::i32;
  }

  vt table_element_type(uint64 table) const
  {
    return table < m.table_types.size() ? from_canonical(m.table_types[size_t(table)]) : vt::ref;
  }

  bool read_block_type(std::vector<vt> *in, std::vector<vt> *out)
  {
    if ( r.pos >= r.size )
    {
      fail("WebAssembly block type is truncated");
      return false;
    }
    const uchar first = r.data[r.pos];
    if ( first == 0x40 )
    {
      ++r.pos;
      return true;
    }
    if ( is_value_type_byte(first) )
    {
      wasmhex_vt::value_type_t decoded;
      if ( wasmhex_vt::decode_value_type(r.data, r.size, &r.pos, &decoded) != wasmhex_vt::vt_ok )
      {
        stop("WebAssembly block type uses an unmodeled reference heap type");
        return false;
      }
      if ( decoded.ref_kind != wasmhex_vt::ref_kind_none )
        stop_typed_ref(); // Typed block types are not modeled
      out->push_back(from_canonical(decoded.canonical_type));
      return true;
    }
    int64 type_index = 0;
    if ( !r.sleb33(&type_index) || type_index < 0 )
    {
      fail("WebAssembly block type is malformed");
      return false;
    }
    if ( uint64(type_index) >= m.types.size() )
    {
      fail("WebAssembly block type has an invalid type index");
      return false;
    }
    *in = convert(m.types[size_t(type_index)].params);
    *out = convert(m.types[size_t(type_index)].results);
    return true;
  }

  // align (with the multi-memory flag) + optional memory index + offset.
  bool read_memarg(unsigned natural_alignment)
  {
    uint64 align = 0;
    if ( !u32(&align) )
      return false;
    uint64 memory = 0;
    if ( (align & 0x40) != 0 )
    {
      align &= ~uint64(0x40);
      if ( !u32(&memory) )
        return false;
    }
    if ( memory >= m.memory_count )
    {
      fail("WebAssembly memory instruction references a missing memory");
      return false;
    }
    if ( align > natural_alignment )
    {
      fail("WebAssembly memory alignment exceeds the natural alignment");
      return false;
    }
    uint64 offset = 0;
    if ( !r.uleb(&offset, m.memory64 ? 64 : 32) )
    {
      fail("WebAssembly memarg offset is malformed");
      return false;
    }
    return true;
  }

  bool read_memory_index()
  {
    uint64 memory = 0;
    return index(&memory, m.memory_count,
                 "WebAssembly memory instruction references a missing memory");
  }

  bool read_table_index(uint64 *table)
  {
    return index(table, m.table_types.size(),
                 "WebAssembly table instruction references a missing table");
  }

  void call(const wasm_type_t &type, bool tail)
  {
    pop(convert(type.params));
    if ( !tail )
    {
      push(convert(type.results));
      return;
    }
    // return_call*: callee results must match the caller's results exactly.
    const std::vector<vt> callee = convert(type.results);
    if ( callee.size() != results.size() )
    {
      fail("WebAssembly tail call result types do not match the caller");
      return;
    }
    for ( size_t i = 0; i < callee.size(); ++i )
      if ( !matches(callee[i], results[i]) )
        fail("WebAssembly tail call result types do not match the caller");
    set_unreachable();
  }

  // instructions

  void unary(vt in, vt out)
  {
    pop(in);
    push(out);
  }
  void binary(vt in, vt out)
  {
    pop(in);
    pop(in);
    push(out);
  }

  void load(vt type, unsigned natural)
  {
    if ( !read_memarg(natural) )
      return;
    pop(memory_address_type());
    push(type);
  }

  // 0xFD prefix: WebAssembly SIMD, modeled through the tables in
  // simd_validator.hpp (final opcode numbering; relaxed SIMD sub-opcodes
  // >= 0x100 stay unsupported on purpose — no corpus module uses them).
  void step_simd()
  {
    uint64 sub = 0;
    if ( !u32(&sub) )
      return;
    if ( sub >= 0x100 )
    {
      stop("relaxed SIMD instructions are not modeled by the validator", error_code::policy_other);
      return;
    }
    const simd_op_info_t info = simd_lookup(uint32(sub));
    if ( !info.known )
    {
      malformed("WebAssembly SIMD instruction has an unknown sub-opcode");
      return;
    }
    unsigned natural = 0;
    while ( (1u << natural) < info.width )
      ++natural;
    uchar lane = 0;
    switch ( info.immediate )
    {
      case SIMD_IMM_MEMARG:
        if ( !read_memarg(natural) )
          return;
        break;
      case SIMD_IMM_MEMARG_LANE:
        if ( !read_memarg(natural) )
          return;
        if ( r.size - r.pos < 1 )
        {
          malformed("WebAssembly lane immediate is truncated");
          return;
        }
        r.byte(&lane);
        if ( lane >= info.lanes )
        {
          fail("WebAssembly lane immediate is out of range");
          return;
        }
        break;
      case SIMD_IMM_LANE:
        if ( r.size - r.pos < 1 )
        {
          malformed("WebAssembly lane immediate is truncated");
          return;
        }
        r.byte(&lane);
        if ( lane >= info.lanes )
        {
          fail("WebAssembly lane immediate is out of range");
          return;
        }
        break;
      case SIMD_IMM_SHUFFLE:
      {
        if ( r.size - r.pos < 16 )
        {
          malformed("WebAssembly shuffle immediate is truncated");
          return;
        }
        uchar indices[16];
        for ( size_t i = 0; i < 16; ++i )
          r.byte(&indices[i]);
        if ( !simd_shuffle_indices_ok(indices) )
        {
          fail("WebAssembly shuffle lane index is out of range");
          return;
        }
        break;
      }
      case SIMD_IMM_CONST16:
        if ( r.size - r.pos < 16 )
        {
          malformed("WebAssembly v128 constant is truncated");
          return;
        }
        skip_bytes(16);
        break;
      case SIMD_IMM_NONE: break;
    }
    // Operands: params[0] is the deepest operand, so pop from the end.
    // For memory forms params[0] is the linear address — its type follows
    // the memory64 flag, not the fixed table entry (the fuzz triage
    // caught wasmhex_fixed_memory64_private rejected as "wrong type").
    for ( size_t i = info.param_count; i-- > 0; )
    {
      const vt expected
        = i == 0 && (info.immediate == SIMD_IMM_MEMARG || info.immediate == SIMD_IMM_MEMARG_LANE)
        ? memory_address_type()
        : from_canonical(info.params[i]);
      pop(expected);
    }
    for ( size_t i = 0; i < info.result_count; ++i )
      push(from_canonical(info.results[i]));
  }

  void store(vt type, unsigned natural)
  {
    if ( !read_memarg(natural) )
      return;
    pop(type);
    pop(memory_address_type());
  }

  void step(uchar opcode)
  {
    switch ( opcode )
    {
      case 0x00: set_unreachable(); return; // unreachable
      case 0x01: return;                    // nop
      case 0x02:                            // block
      case 0x03:                            // loop
      {
        std::vector<vt> in, out;
        if ( !read_block_type(&in, &out) )
          return;
        pop(in);
        push_ctrl(opcode, std::move(in), std::move(out));
        return;
      }
      case 0x04: // if
      {
        std::vector<vt> in, out;
        if ( !read_block_type(&in, &out) )
          return;
        pop(vt::i32);
        pop(in);
        push_ctrl(opcode, std::move(in), std::move(out));
        return;
      }
      case 0x05: // else
      {
        if ( ctrls.back().opcode != 0x04 )
        {
          fail("WebAssembly else without a matching if");
          return;
        }
        frame_t frame;
        if ( pop_ctrl(&frame) )
          push_ctrl(0x05, frame.start_types, frame.end_types);
        return;
      }
      case 0x0B: // end
      {
        frame_t frame;
        if ( !pop_ctrl(&frame) )
          return;
        // An if without else behaves as if the else branch passed its
        // parameters through unchanged.
        if ( frame.opcode == 0x04 && frame.start_types.size() != frame.end_types.size() )
        {
          fail("WebAssembly if without else must have matching parameter and result types");
          return;
        }
        if ( frame.opcode == 0x04 )
          for ( size_t i = 0; i < frame.start_types.size(); ++i )
            if ( !matches(frame.start_types[i], frame.end_types[i]) )
              fail("WebAssembly if without else must have matching parameter and result types");
        if ( !ctrls.empty() )
          push(frame.end_types);
        return;
      }
      case 0x0C: // br
      {
        uint64 depth;
        if ( !index(&depth, ctrls.size(), "WebAssembly branch depth is out of range") )
          return;
        pop(label_types(ctrls[ctrls.size() - 1 - size_t(depth)]));
        set_unreachable();
        return;
      }
      case 0x0D: // br_if
      {
        uint64 depth;
        if ( !index(&depth, ctrls.size(), "WebAssembly branch depth is out of range") )
          return;
        pop(vt::i32);
        const std::vector<vt> types = label_types(ctrls[ctrls.size() - 1 - size_t(depth)]);
        pop(types);
        push(types);
        return;
      }
      case 0x0E: // br_table
      {
        uint64 count;
        if ( !u32(&count) || count > r.size - r.pos )
        {
          fail("WebAssembly br_table is malformed");
          return;
        }
        std::vector<uint64> depths(size_t(count) + 1);
        for ( uint64 &depth : depths )
          if ( !index(&depth, ctrls.size(), "WebAssembly branch depth is out of range") )
            return;
        pop(vt::i32);
        const size_t arity = label_types(ctrls[ctrls.size() - 1 - size_t(depths.back())]).size();
        for ( uint64 depth : depths )
        {
          const std::vector<vt> &types = label_types(ctrls[ctrls.size() - 1 - size_t(depth)]);
          if ( types.size() != arity )
          {
            fail("WebAssembly br_table targets have different arities");
            return;
          }
          // Check against a copy so every target sees the same stack.
          const std::vector<vt> saved = vals;
          pop(types);
          vals = saved;
        }
        pop(label_types(ctrls[ctrls.size() - 1 - size_t(depths.back())]));
        set_unreachable();
        return;
      }
      case 0x0F: // return
        pop(results);
        set_unreachable();
        return;
      case 0x10: // call
      case 0x12: // return_call
      {
        uint64 function;
        if ( !index(&function, function_count(), "WebAssembly call references a missing function") )
          return;
        const wasm_type_t *type = function_signature(function);
        if ( type == nullptr )
        {
          fail("WebAssembly call references a function with an invalid type");
          return;
        }
        call(*type, opcode == 0x12);
        return;
      }
      case 0x11: // call_indirect
      case 0x13: // return_call_indirect
      {
        uint64 type_index, table;
        if ( !index(&type_index, m.types.size(),
                    "WebAssembly call_indirect has an invalid type index")
             || !read_table_index(&table) )
          return;
        if ( table_element_type(table) == vt::externref )
        {
          fail("WebAssembly call_indirect requires a function table");
          return;
        }
        pop(table_address_type(table));
        call(m.types[size_t(type_index)], opcode == 0x13);
        return;
      }
      case 0x1A: pop(); return; // drop
      case 0x1B:                // select
      {
        pop(vt::i32);
        const vt first = pop();
        const vt second = pop();
        if ( is_ref(first) || is_ref(second) )
        {
          fail("WebAssembly untyped select requires numeric or vector operands");
          return;
        }
        if ( !matches(first, second) )
        {
          fail("WebAssembly select operands have different types");
          return;
        }
        push(first == vt::unknown ? second : first);
        return;
      }
      case 0x1C: // select t*
      {
        uint64 count;
        if ( !u32(&count) || count != 1 || r.pos >= r.size || !is_value_type_byte(r.data[r.pos]) )
        {
          fail("WebAssembly typed select must name exactly one value type");
          return;
        }
        wasmhex_vt::value_type_t decoded;
        if ( wasmhex_vt::decode_value_type(r.data, r.size, &r.pos, &decoded) != wasmhex_vt::vt_ok )
        {
          stop("WebAssembly typed select uses an unmodeled heap type");
          return;
        }
        if ( decoded.ref_kind != wasmhex_vt::ref_kind_none )
          stop_typed_ref(); // Typed select types are not modeled
        const vt type = from_canonical(decoded.canonical_type);
        pop(vt::i32);
        pop(type);
        pop(type);
        push(type);
        return;
      }
      case 0x20:
      case 0x21:
      case 0x22: // local.get/set/tee
      {
        uint64 local;
        if ( !index(&local, locals.size(), "WebAssembly local index is out of range") )
          return;
        const vt type = locals[size_t(local)];
        const bool initialized = local_initialized[size_t(local)]; // O(1)
        const bool typed_local = local_ref_forms[size_t(local)] != uchar(wasmhex_vt::ref_kind_none);
        if ( opcode == 0x20 )
        {
          // Runs before the gate below: reading an unassigned
          // non-defaultable local is invalid WebAssembly regardless of how
          // much of the rest we can model.
          if ( local_non_defaultable[size_t(local)] && !initialized )
          {
            fail("WebAssembly local.get reads an unassigned non-defaultable local",
                 error_code::bad_local_assignment);
            return;
          }
          if ( typed_local )
            stop_typed_ref();
          push(type);
        }
        else if ( opcode == 0x21 )
        {
          pop(type);
          if ( local_non_defaultable[size_t(local)] && !initialized )
          {
            local_initialized[size_t(local)] = true;
            if ( !ctrls.empty() )
              ctrls.back().frame_inits.push_back(uint32(local));
          }
          if ( typed_local )
            stop_typed_ref();
        }
        else
        {
          unary(type, type);
          if ( local_non_defaultable[size_t(local)] && !initialized )
          {
            local_initialized[size_t(local)] = true;
            if ( !ctrls.empty() )
              ctrls.back().frame_inits.push_back(uint32(local));
          }
          if ( typed_local )
            stop_typed_ref();
        }
        return;
      }
      case 0x23:
      case 0x24: // global.get/set
      {
        uint64 global;
        if ( !index(&global, m.globals.size(), "WebAssembly global index is out of range") )
          return;
        const global_t &g = m.globals[size_t(global)];
        const vt type = from_canonical(g.type);
        if ( opcode == 0x23 )
        {
          push(type);
          return;
        }
        if ( g.mutability == 0 )
        {
          fail("WebAssembly global.set writes an immutable global");
          return;
        }
        pop(type);
        return;
      }
      case 0x25:
      case 0x26: // table.get/set
      {
        uint64 table;
        if ( !read_table_index(&table) )
          return;
        if ( opcode == 0x25 )
        {
          pop(table_address_type(table));
          push(table_element_type(table));
        }
        else
        {
          pop(table_element_type(table));
          pop(table_address_type(table));
        }
        return;
      }
      // loads: natural alignment is log2 of the access width
      case 0x28: load(vt::i32, 2); return;
      case 0x29: load(vt::i64, 3); return;
      case 0x2A: load(vt::f32, 2); return;
      case 0x2B: load(vt::f64, 3); return;
      case 0x2C:
      case 0x2D: load(vt::i32, 0); return;
      case 0x2E:
      case 0x2F: load(vt::i32, 1); return;
      case 0x30:
      case 0x31: load(vt::i64, 0); return;
      case 0x32:
      case 0x33: load(vt::i64, 1); return;
      case 0x34:
      case 0x35: load(vt::i64, 2); return;
      case 0x36: store(vt::i32, 2); return;
      case 0x37: store(vt::i64, 3); return;
      case 0x38: store(vt::f32, 2); return;
      case 0x39: store(vt::f64, 3); return;
      case 0x3A: store(vt::i32, 0); return;
      case 0x3B: store(vt::i32, 1); return;
      case 0x3C: store(vt::i64, 0); return;
      case 0x3D: store(vt::i64, 1); return;
      case 0x3E: store(vt::i64, 2); return;
      case 0x3F: // memory.size
        if ( read_memory_index() )
          push(memory_address_type());
        return;
      case 0x40: // memory.grow
        if ( read_memory_index() )
          unary(memory_address_type(), memory_address_type());
        return;
      case 0x41:
      {
        int64 value = 0;
        if ( !r.sleb33(&value) || value < INT32_MIN || value > INT32_MAX )
        {
          fail("WebAssembly i32.const is malformed");
          return;
        }
        push(vt::i32);
        return;
      }
      case 0x42:
      {
        int64 value;
        if ( !r.sleb64(&value) )
        {
          fail("WebAssembly i64.const is malformed");
          return;
        }
        push(vt::i64);
        return;
      }
      case 0x43:
        skip_bytes(4);
        push(vt::f32);
        return;
      case 0x44:
        skip_bytes(8);
        push(vt::f64);
        return;
      case 0xD0: // ref.null ht
      {
        int64 heap = 0;
        if ( !r.sleb33(&heap) )
        {
          fail("WebAssembly ref.null is malformed");
          return;
        }
        push(heap == wasmhex_vt::heap_func || heap == -0x0D       ? vt::funcref
               : heap == wasmhex_vt::heap_extern || heap == -0x0E ? vt::externref
                                                                  : vt::ref);
        return;
      }
      case 0xD1: // ref.is_null
      {
        const vt type = pop();
        if ( type != vt::unknown && !is_ref(type) )
          fail("WebAssembly ref.is_null requires a reference operand");
        push(vt::i32);
        return;
      }
      case 0xD2: // ref.func
      {
        uint64 function;
        if ( index(&function, function_count(),
                   "WebAssembly ref.func references a missing function") )
          push(vt::funcref);
        return;
      }
      case 0xFC: step_fc(); return;
      // Families not modeled yet: stop instead of guessing.
      case 0x06:
      case 0x07:
      case 0x08:
      case 0x09:
      case 0x0A:
      case 0x18:
      case 0x19:
      case 0x1F: stop("exception-handling instructions are not modeled by the validator"); return;
      case 0x14:
      case 0x15:
      case 0xD3:
      case 0xD4:
      case 0xD5:
      case 0xD6:
        stop("typed function-reference instructions are not modeled by the validator");
        return;
      case 0xFB: stop("GC instructions are not modeled by the validator"); return;
      case 0xFD: step_simd(); return;
      case 0xFE: step_fe(); return;
      default: break;
    }
    if ( step_numeric(opcode) )
      return;
    fail("WebAssembly function body contains an unknown opcode");
  }

  void skip_bytes(size_t count)
  {
    if ( r.size - r.pos < count )
    {
      fail("WebAssembly constant is truncated");
      return;
    }
    r.pos += count;
  }

  // 0x45..0xC4: numeric instructions, grouped by signature.
  bool step_numeric(uchar op)
  {
    if ( op == 0x45 )
    {
      unary(vt::i32, vt::i32);
      return true;
    }
    if ( op >= 0x46 && op <= 0x4F )
    {
      binary(vt::i32, vt::i32);
      return true;
    }
    if ( op == 0x50 )
    {
      unary(vt::i64, vt::i32);
      return true;
    }
    if ( op >= 0x51 && op <= 0x5A )
    {
      binary(vt::i64, vt::i32);
      return true;
    }
    if ( op >= 0x5B && op <= 0x60 )
    {
      binary(vt::f32, vt::i32);
      return true;
    }
    if ( op >= 0x61 && op <= 0x66 )
    {
      binary(vt::f64, vt::i32);
      return true;
    }
    if ( op >= 0x67 && op <= 0x69 )
    {
      unary(vt::i32, vt::i32);
      return true;
    }
    if ( op >= 0x6A && op <= 0x78 )
    {
      binary(vt::i32, vt::i32);
      return true;
    }
    if ( op >= 0x79 && op <= 0x7B )
    {
      unary(vt::i64, vt::i64);
      return true;
    }
    if ( op >= 0x7C && op <= 0x8A )
    {
      binary(vt::i64, vt::i64);
      return true;
    }
    if ( op >= 0x8B && op <= 0x91 )
    {
      unary(vt::f32, vt::f32);
      return true;
    }
    if ( op >= 0x92 && op <= 0x98 )
    {
      binary(vt::f32, vt::f32);
      return true;
    }
    if ( op >= 0x99 && op <= 0x9F )
    {
      unary(vt::f64, vt::f64);
      return true;
    }
    if ( op >= 0xA0 && op <= 0xA6 )
    {
      binary(vt::f64, vt::f64);
      return true;
    }
    struct conversion_t
    {
      uchar op;
      vt in;
      vt out;
    };
    static const conversion_t conversions[] = {
      { 0xA7, vt::i64, vt::i32 }, { 0xA8, vt::f32, vt::i32 }, { 0xA9, vt::f32, vt::i32 },
      { 0xAA, vt::f64, vt::i32 }, { 0xAB, vt::f64, vt::i32 }, { 0xAC, vt::i32, vt::i64 },
      { 0xAD, vt::i32, vt::i64 }, { 0xAE, vt::f32, vt::i64 }, { 0xAF, vt::f32, vt::i64 },
      { 0xB0, vt::f64, vt::i64 }, { 0xB1, vt::f64, vt::i64 }, { 0xB2, vt::i32, vt::f32 },
      { 0xB3, vt::i32, vt::f32 }, { 0xB4, vt::i64, vt::f32 }, { 0xB5, vt::i64, vt::f32 },
      { 0xB6, vt::f64, vt::f32 }, { 0xB7, vt::i32, vt::f64 }, { 0xB8, vt::i32, vt::f64 },
      { 0xB9, vt::i64, vt::f64 }, { 0xBA, vt::i64, vt::f64 }, { 0xBB, vt::f32, vt::f64 },
      { 0xBC, vt::f32, vt::i32 }, { 0xBD, vt::f64, vt::i64 }, { 0xBE, vt::i32, vt::f32 },
      { 0xBF, vt::i64, vt::f64 }, { 0xC0, vt::i32, vt::i32 }, { 0xC1, vt::i32, vt::i32 },
      { 0xC2, vt::i64, vt::i64 }, { 0xC3, vt::i64, vt::i64 }, { 0xC4, vt::i64, vt::i64 },
    };
    for ( const conversion_t &conversion : conversions )
      if ( conversion.op == op )
      {
        unary(conversion.in, conversion.out);
        return true;
      }
    return false;
  }

  // Wasm threads/atomics (0xFE). The operand shapes mirror their
  // non-atomic memory counterparts (verified against wabt --enable-threads
  // encodings), so a wrong address type, bad memarg or bad alignment makes
  // the module invalid instead of "unsupported" — the atomic family is
  // closed: anything outside the table is malformed.
  void step_fe()
  {
    uint64 sub;
    if ( !u32(&sub) )
      return;
    static const vt access_type[7]
      = { vt::i32, vt::i64, vt::i32, vt::i32, vt::i64, vt::i64, vt::i64 };
    static const unsigned access_natural[7] = { 2, 3, 0, 1, 0, 1, 2 };
    if ( sub <= 2 ) // notify / wait32 / wait64
    {
      if ( !read_memarg(2) )
        return;
      if ( sub == 0x00 )
      {
        pop(vt::i32); // count
        pop(memory_address_type());
        push(vt::i32);
        return;
      }
      pop(vt::i64);                         // timeout
      pop(sub == 0x01 ? vt::i32 : vt::i64); // expected value
      pop(memory_address_type());
      push(vt::i32);
      return;
    }
    if ( sub == 0x03 ) // atomic.fence: one zero flag byte
    {
      uint64 flag;
      if ( !u32(&flag) )
        return;
      if ( flag != 0 )
        fail("WebAssembly atomic fence has a nonzero flag");
      return;
    }
    if ( sub >= 0x10 && sub <= 0x16 ) // atomic loads
    {
      const size_t k = size_t(sub - 0x10);
      load(access_type[k], access_natural[k]);
      return;
    }
    if ( sub >= 0x17 && sub <= 0x1D ) // atomic stores
    {
      const size_t k = size_t(sub - 0x17);
      store(access_type[k], access_natural[k]);
      return;
    }
    if ( sub >= 0x1E && sub <= 0x4E ) // read-modify-write families
    {
      static const uint64 family_base[7] = { 0x1E, 0x25, 0x2C, 0x33, 0x3A, 0x41, 0x48 };
      size_t family = 7;
      for ( size_t f = 0; f < 7; ++f )
        if ( sub >= family_base[f] && sub < family_base[f] + 7 )
        {
          family = f;
          break;
        }
      if ( family == 7 )
      {
        fail("WebAssembly atomic instruction has an unknown subopcode");
        return;
      }
      const size_t k = size_t(sub - family_base[family]);
      if ( !read_memarg(access_natural[k]) )
        return;
      pop(access_type[k]); // value / replacement
      if ( family == 6 )   // cmpxchg also consumes the expected value
        pop(access_type[k]);
      pop(memory_address_type());
      push(access_type[k]);
      return;
    }
    fail("WebAssembly atomic instruction has an unknown subopcode");
  }

  void step_fc()
  {
    uint64 sub;
    if ( !u32(&sub) )
      return;
    static const vt sat_in[8]
      = { vt::f32, vt::f32, vt::f64, vt::f64, vt::f32, vt::f32, vt::f64, vt::f64 };
    static const vt sat_out[8]
      = { vt::i32, vt::i32, vt::i32, vt::i32, vt::i64, vt::i64, vt::i64, vt::i64 };
    if ( sub < 8 )
    {
      unary(sat_in[sub], sat_out[sub]);
      return;
    }
    switch ( sub )
    {
      case 8: // memory.init
      {
        uint64 data;
        if ( !m.has_data_count )
        {
          fail("WebAssembly memory.init requires a data count section");
          return;
        }
        if ( !index(&data, m.declared_data_count,
                    "WebAssembly memory.init references a missing data segment")
             || !read_memory_index() )
          return;
        pop(vt::i32);
        pop(vt::i32);
        pop(memory_address_type());
        return;
      }
      case 9: // data.drop
      {
        uint64 data;
        if ( !m.has_data_count )
        {
          fail("WebAssembly data.drop requires a data count section");
          return;
        }
        index(&data, m.declared_data_count,
              "WebAssembly data.drop references a missing data segment");
        return;
      }
      case 10: // memory.copy
        if ( !read_memory_index() || !read_memory_index() )
          return;
        pop(memory_address_type());
        pop(memory_address_type());
        pop(memory_address_type());
        return;
      case 11: // memory.fill
        if ( !read_memory_index() )
          return;
        pop(memory_address_type());
        pop(vt::i32);
        pop(memory_address_type());
        return;
      case 12: // table.init
      {
        uint64 element, table;
        if ( !u32(&element) || !read_table_index(&table) )
          return;
        pop(vt::i32);
        pop(vt::i32);
        pop(table_address_type(table));
        return;
      }
      case 13: // elem.drop
      {
        uint64 element;
        u32(&element);
        return;
      }
      case 14: // table.copy
      {
        uint64 destination, source;
        if ( !read_table_index(&destination) || !read_table_index(&source) )
          return;
        pop(table_address_type(source));
        pop(table_address_type(source));
        pop(table_address_type(destination));
        return;
      }
      case 15: // table.grow
      {
        uint64 table;
        if ( !read_table_index(&table) )
          return;
        pop(table_address_type(table));
        pop(table_element_type(table));
        push(table_address_type(table));
        return;
      }
      case 16: // table.size
      {
        uint64 table;
        if ( read_table_index(&table) )
          push(table_address_type(table));
        return;
      }
      case 17: // table.fill
      {
        uint64 table;
        if ( !read_table_index(&table) )
          return;
        pop(table_address_type(table));
        pop(table_element_type(table));
        pop(table_address_type(table));
        return;
      }
      default: stop("0xFC sub-opcode is not modeled by the validator"); return;
    }
  }
};

} // namespace

function_validation_t validate_function_body(const module_t &module,
                                             const wasm_type_t &function_type,
                                             const std::vector<uchar> &local_types,
                                             const uchar *body, size_t size,
                                             const std::vector<uchar> *local_ref_forms)
{
  validator_t validator(module, function_type, local_types, body, size, local_ref_forms);
  return validator.run();
}

} // namespace wasmhex::loader
