;; WABT 1.0.42 cannot assemble type-indexed GC references; the paired binary uses their core encodings.
(module
  (type $record (sub (struct (field i32))))
  (table (export "shared") 2 (ref null $record))
  (elem (table 0) (i32.const 0) (ref null $record) (ref.null $record)))
