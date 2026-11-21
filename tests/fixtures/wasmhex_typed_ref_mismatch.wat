(module
  (type $source (func))
  (type $target (func (param i32)))
  (func (export "typed_ref_mismatch")
      (param (ref null $source)) (result (ref null $target))
    (block (result (ref null $target))
      local.get 0
      br_on_non_null 0
      ref.null $target)))
