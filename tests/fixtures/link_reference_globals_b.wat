(module
  (import "env" "fun" (global $fun funcref))
  (import "env" "ext" (global $ext externref))
  (export "fun_alias_b" (global $fun))
  (export "ext_alias_b" (global $ext))
)
