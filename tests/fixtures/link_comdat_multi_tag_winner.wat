(module
  (type $i32_tag_type (func (param i32)))
  (type $i64_tag_type (func (param i64)))
  (tag $tag_a (type $i32_tag_type))
  (tag $tag_b (type $i64_tag_type))
  (export "tag_a" (tag $tag_a))
  (export "tag_b" (tag $tag_b))
)
