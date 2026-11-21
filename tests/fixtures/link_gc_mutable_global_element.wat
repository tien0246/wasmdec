;; Invalid by design: a constant expression cannot read a mutable global.
(module
  (type $record (struct (field i32)))
  (import "env" "shared" (table 2 (ref null $record)))
  (global $record_value (mut (ref null $record)) (ref.null $record))
  (elem (table 0) (i32.const 0) (ref null $record) (global.get $record_value)))
