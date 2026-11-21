(module
  (type $tag_type (func (param i32)))
  (func (export "prefix_func"))
  (table 1 funcref)
  (global (mut i32) (i32.const 0))
  (tag $prefix_tag (type $tag_type))
  (data "p")
)
