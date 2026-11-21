(module
  (type $unary (func (param i32) (result i32)))
  (import "env" "table" (table 1 1 funcref))

  (func $table_entry (type $unary)
    local.get 0
    i32.const 77
    i32.add)
  (elem (table 0) (i32.const 0) func $table_entry)

  (func $wasm_indirect (export "indirect") (param $value i32) (param $index i32) (result i32)
    local.get $value
    local.get $index
    call_indirect (type $unary)))
