;; WABT 1.0.42 cannot assemble type-indexed GC references; the paired binary uses their core encodings.
(module
  (type $prefix (func (param f32)))
  (type $items (array (mut i32)))
  (table (export "shared") 2 (ref null $items))
  (elem (table 0) (i32.const 0) (ref null $items) (ref.null $items)))
