#pragma once
#include "../../../ida/processor/internal.hpp"
namespace wasmhex
{
// Infer struct-pointer / scalar-pointer types for locals that are used
// as deref bases with constant byte offsets. Call once per decompilation, at
// CMAT_FINAL, after the other ctree rewriters.
void apply_struct_pointer_inference(cfunc_t *cfunc);

// Build a C string literal expression ("text", char[len+1], EXFL_CSTR)
// for display swaps — the same shape rewriter_idiom's __memory_base path
// produces. The caller owns the returned expression until replace_by().
// EA is the literal's anchor: the linear-memory DATA EA of the string
// bytes, never an instruction EA — Hex-Rays persists final ctree literals
// into the IDB synthetic-string registry at that EA (Strings window).
cexpr_t *make_go_string_literal(const qstring &text, ea_t ea);

// C string args: build an `&item` address expression
// (cot_ref over cot_obj at `ea`) for a constant argument PROVEN to be a
// data/BSS address (pointer-typed parameter + in-span constant). Display
// only, exactly like make_go_string_literal; the item itself comes from the
// loader's dref (no IDB mutation here). The caller owns the returned
// expression until replace_by().
cexpr_t *make_data_address_literal(ea_t ea);
}
