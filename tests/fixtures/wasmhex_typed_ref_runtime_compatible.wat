(module
  (type $target (func))
  (func (export "identity")
      (param (ref null $target)) (result (ref null $target))
    local.get 0))
