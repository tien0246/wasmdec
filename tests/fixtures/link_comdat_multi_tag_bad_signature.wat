(module
  (type $i32_tag_type (func (param i32)))
  (type $f64_tag_type (func (param f64)))
  (tag $tag_a (type $i32_tag_type))
  (tag $tag_b (type $f64_tag_type))
  (global $keep i32 (i32.const 1))
)
