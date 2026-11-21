(module
  (table $same 2 funcref)
  (table $helper 1 funcref)
  (func (export "same_size") (result i32) (table.size $same))
  (func (export "grow_same") (result i32) (ref.null func) (i32.const 1) (table.grow $same))
  (func (export "helper_size") (result i32) (table.size $helper))
  (export "helper" (table $helper))
)
