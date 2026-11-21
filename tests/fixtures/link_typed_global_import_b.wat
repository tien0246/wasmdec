(module
  (type $prefix (func (param f64)))
  (type $ft (func))
  (import "env" "typed" (global $g (ref null $ft)))
  (export "typed_import_b" (global $g))
)
