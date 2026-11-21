(module
  (type $tag_type (func (param i64)))
  (tag $same (type $tag_type))
  (tag $helper (type $tag_type))
  (export "helper" (tag $helper))
)
