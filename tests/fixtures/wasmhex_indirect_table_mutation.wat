(module
  (type $empty (func))
  (table 1 funcref)
  (func $target (type $empty))
  (elem (i32.const 0) func $target)
  (func (export "mutate_table") (param i32 funcref)
    local.get 0
    local.get 1
    table.set 0))
