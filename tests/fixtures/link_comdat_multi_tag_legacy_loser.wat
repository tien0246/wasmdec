(module
  (type $i32_tag_type (func (param i32)))
  (type $void_tag_type (func))
  (tag $sig_i32 (type $i32_tag_type))
  (tag $sig_void (type $void_tag_type))
  (func (export "legacy_run")
    (try
      (do
        (i32.const 17)
        (throw $sig_i32))
      (catch $sig_i32
        (drop))))
  (func (export "legacy_normal_run")
    (try
      (do
        (nop))
      (catch $sig_i32
        (drop))))
  (func (export "legacy_all_run")
    (try
      (do
        (throw $sig_void))
      (catch_all
        (nop))))
)
