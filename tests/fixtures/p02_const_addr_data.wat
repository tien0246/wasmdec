;; P0-2 fixture (regression guard): load/store through a constant address
;; inside the initialized data span. This is the sanctioned
;; constant-as-address path (make_effective_memory_address): the address
;; operand must stay a global-variable reference into the linear-memory
;; segment (mop_v gvar) so Hex-Rays shows the data symbol, not a number.
(module
  (memory 1)
  (data (i32.const 1024) "\2a\00\00\00")
  (func (export "read_answer") (result i32)
    i32.const 1024
    i32.load)
  (func (export "write_answer") (param i32)
    i32.const 1024
    local.get 0
    i32.store))
