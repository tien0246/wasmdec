(module
  (type $i32 (func (param i32) (result i32)))
  (type $i64 (func (param i64) (result i64)))
  (type $f32 (func (param f32) (result f32)))
  (type $f64 (func (param f64) (result f64)))
  (type $mixed (func (param i32 i64 f32 f64) (result i64)))
  (type $void (func (param i32)))
  (type $extern (func (param externref) (result externref)))
  (type $function (func (param funcref) (result funcref)))
  (import "env" "identity" (func $imported_i32 (type $i32)))
  (export "identity_import" (func $imported_i32))

  (func $target_i32 (type $i32)
    local.get 0
    i32.const 1
    i32.add)
  (export "target_i32" (func $target_i32))
  (func (export "call_ref_i32") (type $i32)
    local.get 0
    ref.func $target_i32
    call_ref $i32)
  (func (export "return_call_ref_i32") (type $i32)
    local.get 0
    ref.func $target_i32
    return_call_ref $i32)
  (func (export "call_ref_local_i32") (param i32) (result i32) (local (ref null $i32))
    ref.func $target_i32
    local.set 1
    local.get 0
    local.get 1
    call_ref $i32)
  (func (export "call_ref_tee_i32") (param i32) (result i32) (local (ref null $i32))
    ref.func $target_i32
    local.tee 1
    drop
    local.get 0
    local.get 1
    call_ref $i32)

  (func $target_i64 (type $i64)
    local.get 0
    i64.const 1
    i64.add)
  (export "target_i64" (func $target_i64))
  (func (export "call_ref_i64") (type $i64)
    local.get 0
    ref.func $target_i64
    call_ref $i64)
  (func (export "return_call_ref_i64") (type $i64)
    local.get 0
    ref.func $target_i64
    return_call_ref $i64)

  (func $target_f32 (type $f32)
    local.get 0)
  (export "target_f32" (func $target_f32))
  (func (export "call_ref_f32") (type $f32)
    local.get 0
    ref.func $target_f32
    call_ref $f32)

  (func $target_f64 (type $f64)
    local.get 0)
  (export "target_f64" (func $target_f64))
  (func (export "call_ref_f64") (type $f64)
    local.get 0
    ref.func $target_f64
    call_ref $f64)

  (func $target_mixed (type $mixed)
    local.get 1)
  (export "target_mixed" (func $target_mixed))
  (func (export "call_ref_mixed") (type $mixed)
    local.get 0
    local.get 1
    local.get 2
    local.get 3
    ref.func $target_mixed
    call_ref $mixed)
  (func (export "return_call_ref_mixed") (type $mixed)
    local.get 0
    local.get 1
    local.get 2
    local.get 3
    ref.func $target_mixed
    return_call_ref $mixed)

  (func $target_void (type $void)
    local.get 0
    drop)
  (export "target_void" (func $target_void))
  (func (export "call_ref_void") (type $void)
    local.get 0
    ref.func $target_void
    call_ref $void)

  (func (export "call_ref_import") (type $i32)
    local.get 0
    ref.func $imported_i32
    call_ref $i32)
  (func (export "return_call_ref_import") (type $i32)
    local.get 0
    ref.func $imported_i32
    return_call_ref $i32)

  (func $target_extern (type $extern)
    local.get 0)
  (export "target_extern" (func $target_extern))
  (func (export "call_ref_extern") (type $extern)
    local.get 0
    ref.func $target_extern
    call_ref $extern)

  (func $target_function (type $function)
    local.get 0)
  (export "target_function" (func $target_function))
  (func (export "call_ref_function") (type $function)
    local.get 0
    ref.func $target_function
    call_ref $function))
