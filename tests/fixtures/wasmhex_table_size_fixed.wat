(module
  (table $fixed 3 3 funcref)
  (table $growable 2 5 funcref)
  (table $unbounded 2 funcref)

  (func (export "fixed_size") (result i32)
    table.size $fixed)

  (func (export "growable_size") (result i32)
    table.size $growable)

  (func (export "unbounded_size") (result i32)
    table.size $unbounded))
