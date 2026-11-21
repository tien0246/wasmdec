;; WABT 1.0.42 cannot assemble type-indexed GC references; the paired binary uses their core encodings.
(module
  (type $record (sub (struct (field i32))))
  (type $child (sub $record (struct (field i32) (field i64))))
  (import "env" "shared" (table 2 (ref null $record)))
  (global $child_value (ref null $child) (ref.null $child))
  (elem (table 0) (i32.const 1) (ref null $record) (global.get $child_value)))
