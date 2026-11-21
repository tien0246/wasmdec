(module
  (import "env" "echo_ref" (func $echo_ref (param externref) (result funcref)))
  (import "env" "select_ref" (func $select_ref (param funcref externref) (result externref)))

  (func (export "wasm_ref_bridge") (param externref) (result funcref)
    local.get 0
    call $echo_ref)

  (func (export "wasm_ref_pair_bridge") (param funcref externref) (result externref)
    local.get 0
    local.get 1
    call $select_ref))
