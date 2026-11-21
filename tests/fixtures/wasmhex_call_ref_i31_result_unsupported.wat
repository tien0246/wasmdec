(; WABT 1.0.42 cannot encode i31ref in a function signature; the checker builds its Core binary directly. ;)
(module
  (type $tuple (func (param i32) (result i32 (ref null i31))))
  (import "env" "i31" (func $i31 (type $tuple)))
  (elem declare func $i31)
  (func (export "i31_result") (param i32) (result i32 (ref null i31))
    local.get 0
    ref.func $i31
    call_ref $tuple))
