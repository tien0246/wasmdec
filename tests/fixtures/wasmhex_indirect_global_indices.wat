(module
  (type $unary_i32 (func (param i32) (result i32)))
  (import "env" "index32" (global $imported_index32 i32))
  (table $targets32 4 funcref)
  (table $targets64 i64 4 funcref)
  (global $index32 i32 (i32.const 1))
  ;; The Python test rewrites these initializers to global.get aliases.
  (global $index32_alias i32 (i32.const 1))
  (global $index32_alias2 i32 (i32.const 1))
  (global $index64 i64 (i64.const 2))
  (global $index64_alias i64 (i64.const 2))
  (global $mutable_index32 (mut i32) (i32.const 1))
  (func $target0 (export "target0") (type $unary_i32)
    (param i32) (result i32)
    local.get 0
    i32.const 10
    i32.add)
  (func $target1 (export "target1") (type $unary_i32)
    (param i32) (result i32)
    local.get 0
    i32.const 20
    i32.add)
  (func $target2 (export "target2") (type $unary_i32)
    (param i32) (result i32)
    local.get 0
    i32.const 30
    i32.add)
  (func $target3 (export "target3") (type $unary_i32)
    (param i32) (result i32)
    local.get 0
    i32.const 40
    i32.add)
  (elem (table $targets32) (i32.const 0)
    func $target0 $target1 $target2 $target3)
  (elem (table $targets64) (i64.const 0)
    func $target0 $target1 $target2 $target3)
  (func (export "global_index32") (param i32) (result i32)
    local.get 0
    global.get $index32
    call_indirect $targets32 (type $unary_i32))
  (func (export "global_alias32") (param i32) (result i32)
    local.get 0
    global.get $index32_alias2
    call_indirect $targets32 (type $unary_i32))
  (func (export "global_local_alias32") (param i32) (result i32) (local i32)
    global.get $index32
    local.set 1
    local.get 0
    local.get 1
    call_indirect $targets32 (type $unary_i32))
  (func (export "global_index64") (param i32) (result i32)
    local.get 0
    global.get $index64
    call_indirect $targets64 (type $unary_i32))
  (func (export "global_alias64") (param i32) (result i32)
    local.get 0
    global.get $index64_alias
    call_indirect $targets64 (type $unary_i32))
  (func (export "global_local_alias64") (param i32) (result i32) (local i64)
    global.get $index64
    local.set 1
    local.get 0
    local.get 1
    call_indirect $targets64 (type $unary_i32))
  (func (export "mutable_global_index") (param i32) (result i32)
    local.get 0
    global.get $mutable_index32
    call_indirect $targets32 (type $unary_i32))
  (func (export "imported_global_index") (param i32) (result i32)
    local.get 0
    global.get $imported_index32
    call_indirect $targets32 (type $unary_i32)))
