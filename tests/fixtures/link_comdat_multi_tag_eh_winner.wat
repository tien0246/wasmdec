(module
  (type $i32_tag_type (func (param i32)))
  (type $void_tag_type (func))
  (tag $sig_i32 (type $i32_tag_type))
  (tag $sig_void (type $void_tag_type))
  (export "sig_i32" (tag $sig_i32))
  (export "sig_void" (tag $sig_void))
)
