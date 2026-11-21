(module
  (import "env" "readonly" (global $readonly i32))
  (import "env" "counter32" (global $counter32 (mut i32)))
  (import "env" "counter64" (global $counter64 (mut i64)))
  (import "env" "ratio32" (global $ratio32 (mut f32)))
  (import "host" "scale64" (global $scale64 (mut f64)))

  (func (export "read_readonly") (result i32)
    global.get $readonly)
  (func (export "get_counter32") (result i32)
    global.get $counter32)
  (func (export "set_counter32") (param $value i32)
    local.get $value
    global.set $counter32)
  (func (export "get_counter64") (result i64)
    global.get $counter64)
  (func (export "set_counter64") (param $value i64)
    local.get $value
    global.set $counter64)
  (func (export "get_ratio32") (result f32)
    global.get $ratio32)
  (func (export "set_ratio32") (param $value f32)
    local.get $value
    global.set $ratio32)
  (func (export "get_scale64") (result f64)
    global.get $scale64)
  (func (export "set_scale64") (param $value f64)
    local.get $value
    global.set $scale64))
