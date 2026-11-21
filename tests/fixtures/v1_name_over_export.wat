;; V1 name-priority witness: the same function carries an export name
;; ("public") and a name-section name ("internal_symbol") — the name
;; section must win; with no name section the export applies.
(module
  (func $internal_symbol (export "public") (result i32) i32.const 7)
  (func (export "only_export") (result i32) i32.const 9))
