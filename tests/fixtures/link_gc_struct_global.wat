;; WABT 1.0.42 cannot assemble type-indexed GC references; the paired binary uses their core encodings.
(module
  (type $leaf (struct (field i32)))
  (type $record (struct (field (ref null $leaf))))
  (import "env" "shared" (table 2 (ref null $record)))
  (global $record_value (ref null $record) (ref.null $record))
  (elem (table 0) (i32.const 1) (ref null $record) (global.get $record_value)))
