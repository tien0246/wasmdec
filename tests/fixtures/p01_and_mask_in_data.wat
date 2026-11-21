;; P0-1 fixture: `i32.and` with a mask whose value lands inside the mapped
;; data span. Before P0-1 every constant in the span was rewritten into a
;; memory read of the DATA mirror (`x & word_4000FFFF`); the mask must stay
;; the number 0xFFFF. The second data segment stretches the span past
;; offset 0xFFFF so the mask value is covered exactly like main.wasm's.
(module
  (memory 1)
  (data (i32.const 16) "hello p01\00")
  (data (i32.const 65528) "\aa\bb\cc\dd\ee\ff\ff\00")
  (func (export "masked") (param i32) (result i32)
    local.get 0
    i32.const 65535
    i32.and)
  (func (export "masked64") (param i64) (result i64)
    local.get 0
    i64.const 65535
    i64.and))
