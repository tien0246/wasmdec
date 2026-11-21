(module
  (type $tag_type (func (param i32)))
  (tag $signal (type $tag_type))
  (export "signal" (tag $signal))
  (func (export "thrower") (i32.const 42) (throw $signal))
)
