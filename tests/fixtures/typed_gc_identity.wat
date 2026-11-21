;; WABT 1.0.42 cannot assemble type-indexed GC references; the paired binary uses their core encodings.
(module
  (type $record (sub (struct (field i32))))
  (type $record_child (sub $record (struct (field i32) (field i32))))
  (type $items (array i32))
  (table $records 1 (ref null $record))
  (global $record_global (mut (ref null $record)) (ref.null $record))
  (func (export "struct_null_test") (result i32)
    ref.null $record
    ref.test (ref null $record))
  (func (export "array_null_test") (result i32)
    ref.null $items
    ref.test (ref null $items))
  (func (export "child_subtype_test") (result i32)
    ref.null $record
    ref.test (ref null $record_child))
  (func (export "global_identity") (result i32)
    ref.null $record
    global.set $record_global
    global.get $record_global
    ref.test (ref null $record))
  (func (export "local_cast_identity") (result i32)
    (local $record_local (ref null $record))
    ref.null $record
    local.tee $record_local
    ref.cast (ref null $record)
    ref.test (ref null $record))
  (func (export "cast_from_eqref") (result i32)
    (local $generic eqref)
    ref.null eq
    local.tee $generic
    ref.cast (ref null $record)
    ref.test (ref null $record))
  (func (export "nonnull_local_cast") (result i32)
    (local $record_nonnull (ref $record))
    ref.null $record
    ref.as_non_null
    local.tee $record_nonnull
    ref.cast (ref $record)
    ref.test (ref $record)))
