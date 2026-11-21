(module
  ;; C++6/K1: two functions with byte-identical bodies but names from
  ;; different families (an ICF fold in a real link), plus a unique-body
  ;; function. Calls into the folded pair annotate the callee with a
  ;; repeatable function comment listing the other folded name; the
  ;; unique function gets nothing.
  (import "env" "ext_dummy" (func $ext_dummy (param i32)))
  (memory 1)
  (func $alpha_family::alpha (export "alpha") (param i32 i32) (result i32)
    (local i32)
    local.get 0
    i32.const 4
    i32.add
    local.tee 2
    i32.load offset=8
    call $ext_dummy
    local.get 2
    i32.load offset=12)
  (func $beta_family::beta (export "beta") (param i32 i32) (result i32)
    ;; identical body to alpha — the linker (or a template instantiation)
    ;; folding case
    (local i32)
    local.get 0
    i32.const 4
    i32.add
    local.tee 2
    i32.load offset=8
    call $ext_dummy
    local.get 2
    i32.load offset=12)
  (func $gamma_unique (export "gamma_unique") (param i32) (result i32)
    local.get 0
    i32.const 7
    i32.mul)
  (func (export "cpp6_call_folded") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    call $beta_family::beta)
  (func (export "cpp6_call_unique") (param i32) (result i32)
    local.get 0
    call $gamma_unique)
)
