;; WABT 1.0.42 cannot assemble type-indexed GC references; the paired binary uses their core encodings.
(module
  (type $items (array (mut i64)))
  (import "env" "shared" (table 2 (ref null $items))))
