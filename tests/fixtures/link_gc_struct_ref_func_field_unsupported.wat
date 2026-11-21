;; The paired binary is valid, but the linker deliberately rejects global.get struct fields.
(module
  (type $fn (func))
  (type $item (struct (field (ref null $fn))))
  (import "env" "g" (global (ref null $fn)))
  (table 1 (ref null $item))
  (elem (table 0) (i32.const 0) (ref null $item)
    (struct.new $item (global.get $g)))
)
