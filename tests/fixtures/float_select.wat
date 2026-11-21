(module
  (func (export "test") (result i32)
    (local $condition i32)
    f32.const 1
    f32.const 2
    f32.lt
    local.set $condition
    f32.const 3
    f32.const 4
    local.get $condition
    select
    i32.trunc_f32_s))
