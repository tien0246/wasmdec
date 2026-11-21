(module
  (import "env" "ext" (global $ext externref))
  (global $copy externref (global.get $ext))
)
