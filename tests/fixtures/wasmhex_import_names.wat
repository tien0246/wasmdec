(module
  (import "env" "invoke" (func $env_invoke (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "invoke"
    (func $wasi_invoke (param i32) (result i32)))
  (func (export "call_env") (param i32) (result i32)
    local.get 0
    call $env_invoke)
  (func (export "call_wasi") (param i32) (result i32)
    local.get 0
    call $wasi_invoke))
