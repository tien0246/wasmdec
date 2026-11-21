(module
  (global $g_i32 (mut i32) (i32.const 17))
  (global $g_i64 (mut i64) (i64.const 1234605616436508552))
  (global $g_f32 (mut f32) (f32.const 1.25))
  (global $g_f64 (mut f64) (f64.const -3.5))

  (func (export "get_i32") (result i32)
    global.get $g_i32)
  (func (export "set_i32") (param $value i32)
    local.get $value
    global.set $g_i32)
  (func (export "get_i64") (result i64)
    global.get $g_i64)
  (func (export "set_i64") (param $value i64)
    local.get $value
    global.set $g_i64)
  (func (export "get_f32") (result f32)
    global.get $g_f32)
  (func (export "set_f32") (param $value f32)
    local.get $value
    global.set $g_f32)
  (func (export "get_f64") (result f64)
    global.get $g_f64)
  (func (export "set_f64") (param $value f64)
    local.get $value
    global.set $g_f64))
