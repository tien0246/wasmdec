#pragma once
#include "../../../ida/processor/internal.hpp"
namespace wasmhex
{

// Shared statement eraser (defined in ctree.cpp).
void remove_block_statements(cfunc_t *cfunc, const std::set<cinsn_t *> &targets);

// Per-rewriter entry points (one translation unit each).
void wasmhex_apply_jumpout_rewriter(cfunc_t *cfunc);
void wasmhex_apply_shift_rewriter(cfunc_t *cfunc);
void wasmhex_apply_idiom_rewriter(cfunc_t *cfunc);
void wasmhex_apply_stack_import_rewriter(cfunc_t *cfunc);
void wasmhex_apply_eh_rewriter(cfunc_t *cfunc);
void wasmhex_apply_cpp_rewriter(cfunc_t *cfunc);
void wasmhex_apply_copy_fuse_rewriter(cfunc_t *cfunc);
void wasmhex_apply_go_rewriter(cfunc_t *cfunc);

ssize_t idaapi wasmhex_hexrays_callback(void *, hexrays_event_t event, va_list va);
}
