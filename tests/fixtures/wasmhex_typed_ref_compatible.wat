(module
  (type $target (func))
  (type $ref_result (func (result (ref null $target))))
  (func $target_function (export "target"))
  (func (export "typed_br_on_null")
      (param $payload (ref null $target))
      (param $condition (ref null $target))
      (result (ref null $target))
    (block $done (type $ref_result)
      local.get $payload
      local.get $condition
      br_on_null $done
      drop))
  (func (export "typed_br_on_non_null")
      (param $candidate (ref null $target))
      (param $fallback (ref null $target))
      (result (ref null $target))
    (block $done (type $ref_result)
      local.get $candidate
      br_on_non_null $done
      local.get $fallback))
)
