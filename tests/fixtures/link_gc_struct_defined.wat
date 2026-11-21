;; WABT 1.0.42 cannot assemble type-indexed GC references; the paired binary uses their core encodings.
(module
  (type $prefix (func (param f32)))
  (type $leaf (struct (field i32)))
  (type $record (struct (field (ref null $leaf))))
  (table (export "shared") 2 (ref null $record))
  (elem (table 0) (i32.const 0) (ref null $record) (ref.null $record)))
