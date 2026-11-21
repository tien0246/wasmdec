(module
  (type $tag_type (func (param i32)))
  (import "env" "signal" (tag $signal (type $tag_type)))
  (export "signal_a" (tag $signal))
)
