;; Deliberately invalid: the i64 address does not match i32.atomic.load's i32 address.
(module
  (memory 1 1 shared)
  (func (export "bad_atomic_address_type") (param i64) (result i32)
    local.get 0
    i32.atomic.load align=4))
