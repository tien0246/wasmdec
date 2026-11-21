;; M1-S3 pinned fixture (SEMANTICS.md §5b): a module that uses the C
;; shadow stack pointer WITHOUT a canonical entry prologue. The loader
;; must diagnose it ("C shadow stack fallback: uses C stack pointer
;; without a canonical entry prologue") and F5 must still succeed with
;; the marker degraded through the sp-global linear address (lowering
;; contract 1/2/4 of §5b).
(module
  (import "env" "__stack_pointer" (global $sp (mut i32)))
  (func (export "poke") (result i32)
    global.get $sp
    i32.const 4
    i32.add))
