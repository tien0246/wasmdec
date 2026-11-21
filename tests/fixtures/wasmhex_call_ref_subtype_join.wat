;; WABT 1.0.42 cannot parse textual function subtypes; the checked-in binary is
;; encoded with this type relation and validated/executed by Node.
(module
  (type $base (sub (func (param i32) (result i32))))
  (type $child (sub final $base (func (param i32) (result i32))))
  (type $dispatch_type (func (param i32 i32) (result i32)))
  (func $base_target (type $base) local.get 0)
  (func $child_target (type $child) local.get 0)
  (elem declare func $base_target $child_target)
  (func (export "dispatch_subtype_join") (type $dispatch_type)
      (local (ref null $base))
    local.get 1
    if
      ref.func $base_target
      local.set 2
    else
      ref.func $child_target
      local.set 2
    end
    local.get 0
    local.get 2
    call_ref $base))
