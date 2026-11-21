(module
  (import "env" "fun" (global $fun funcref))
  (import "env" "ext" (global $ext externref))
  (export "fun_alias_a" (global $fun))
  (export "ext_alias_a" (global $ext))
)
