(module
  (type $ft (func (param i32)))
  (global $g (ref null $ft) (ref.null $ft))
  (export "typed" (global $g))
)
