;; WABT 1.0.42 cannot assemble type-indexed GC references; the paired binary uses their core encodings.
(module
  (type $record (struct (field i32)))
  (import "env" "object" (global (ref $record)))
  (table 1 (ref null $record))
  (elem (table 0) (i32.const 0) (ref $record) (global.get 0)))
