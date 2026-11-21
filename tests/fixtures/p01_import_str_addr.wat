;; P0-1 fixture: a constant string address passed to an import. The
;; argument is the linear-memory offset 16 (where "hi" lives); it must
;; render as the plain number 16 — never as a memory read of the data
;; segment (the old `a1: word_…` misreading) and never as a .text label.
(module
  (import "env" "print" (func $print (param i32)))
  (memory 1)
  (data (i32.const 16) "hi\00")
  (func (export "say_hi")
    i32.const 16
    call $print))
