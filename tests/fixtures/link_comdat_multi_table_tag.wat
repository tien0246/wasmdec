(module
  (type $tag_type (func (param i32)))
  (table $table_a 1 funcref)
  (table $table_b 1 funcref)
  (tag $tag_a (type $tag_type))
  (tag $tag_b (type $tag_type))
  (global $keep i32 (i32.const 0))
)
