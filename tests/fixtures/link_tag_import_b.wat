(module
  (type $prefix (func (param f64)))
  (type $tag_type (func (param i32)))
  (import "env" "signal" (tag $signal (type $tag_type)))
  (export "signal_b" (tag $signal))
)
