(module
  (type $pair (func (result i32 i64)))
  (type $triple (func (result i32 i64 externref)))

  (func $ref_sink (export "ref_sink") (param externref) (result i32)
    i32.const 5)
  (func $ref_discard (export "ref_discard") (param externref))

  (func (export "br_on_null_value") (param externref) (result i32)
    (block $done (result i32)
      i32.const 10
      local.get 0
      br_on_null $done
      call $ref_sink
      i32.add))

  (func (export "br_on_null_empty") (param externref) (result i32)
    (block
      local.get 0
      br_on_null 0
      call $ref_discard)
    i32.const 42)

  (func (export "br_on_non_null") (param externref externref) (result externref)
    (block $done (result externref)
      local.get 0
      br_on_non_null $done
      local.get 1))

  (func (export "br_on_null_pair") (param externref) (result i32 i64)
      (local i32 i64)
    (block $done (type $pair)
      i32.const 10
      i64.const 20
      local.get 0
      br_on_null $done
      call $ref_discard
      local.set 2
      local.set 1
      i32.const 11
      i64.const 21))

  (func (export "br_on_non_null_triple")
      (param externref externref) (result i32 i64) (local i32 i64)
    (block $done (type $triple)
      i32.const 10
      i64.const 20
      local.get 0
      br_on_non_null $done
      local.set 3
      local.set 2
      i32.const 11
      i64.const 21
      local.get 1)
    call $ref_discard))
