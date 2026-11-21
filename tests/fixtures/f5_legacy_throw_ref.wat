(module
  (type $empty_type (func))
  (tag $empty (type $empty_type))

  (func (export "throw_ref_catch_all") (param exnref)
    (try
      (do
        (local.get 0)
        (throw_ref))
      (catch_all
        (nop))))

  (func (export "throw_ref_typed_catch") (param exnref)
    (try
      (do
        (local.get 0)
        (throw_ref))
      (catch $empty
        (nop))
      (catch_all
        (nop)))))
