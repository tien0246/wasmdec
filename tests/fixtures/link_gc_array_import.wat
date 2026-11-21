;; WABT 1.0.42 cannot assemble type-indexed GC references; the paired binary uses their core encodings.
(module
  (type $items (array (mut i32)))
  (import "env" "shared" (table 2 (ref null $items)))
  (elem (table 0) (i32.const 1) (ref null $items) (ref.null $items)))
