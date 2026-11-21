(module
  (import "env" "refs" (table 2 externref))
  (import "env" "g" (global externref))
  (elem (table 0) (i32.const 0) externref (global.get 0))
)
