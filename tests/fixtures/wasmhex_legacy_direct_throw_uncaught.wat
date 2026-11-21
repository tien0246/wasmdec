(module
  (type $empty (func))
  (tag $signal (type $empty))
  (func (export "uncaught")
    (throw $signal)))
