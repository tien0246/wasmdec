(module
  (import "env" "shared" (global $shared i32))
  (global $copy i32 (global.get $shared))
)
