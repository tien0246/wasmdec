(module
  (type $tag_type (func))
  (tag $signal (type $tag_type))
  (func
    (try_table (catch $signal 0)
      (nop)))
)
