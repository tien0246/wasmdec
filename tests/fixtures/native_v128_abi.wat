(module
  (type $vector_to_vector (func (param v128) (result v128)))
  (type $vector_to_i64 (func (param v128) (result i64)))
  (type $vector_to_i32 (func (param v128) (result i32)))
  (import "env" "transform" (func $transform (type $vector_to_i64)))
  (table 5 funcref)

  (func $identity (export "identity") (type $vector_to_vector)
    local.get 0)
  (func $forward (export "forward") (type $vector_to_vector)
    local.get 0 call $identity)
  (func $make (export "make") (param i64) (result v128)
    local.get 0 i64x2.splat)

  (func $extract (export "extract") (type $vector_to_i64)
    local.get 0 i64x2.extract_lane 1)
  (func $forward_extract (export "forward_extract") (type $vector_to_i64)
    local.get 0 call $extract)
  (func $import_extract (export "import_extract") (type $vector_to_i64)
    local.get 0 call $transform)
  (func $indirect_extract (export "indirect_extract") (param v128 i32) (result i64)
    local.get 0 local.get 1 call_indirect (type $vector_to_i64))
  (elem (i32.const 0) func $extract)

  (func $read_lane0 (export "read_lane0") (type $vector_to_i32)
    local.get 0 i32x4.extract_lane 0)
  (func $read_lane1 (export "read_lane1") (type $vector_to_i32)
    local.get 0 i32x4.extract_lane 1)
  (func $read_lane2 (export "read_lane2") (type $vector_to_i32)
    local.get 0 i32x4.extract_lane 2)
  (func $read_lane3 (export "read_lane3") (type $vector_to_i32)
    local.get 0 i32x4.extract_lane 3)
  (elem (i32.const 1)
    func $read_lane0 $read_lane1 $read_lane2 $read_lane3)
  (func $indirect_lane (export "indirect_lane") (param v128 i32) (result i32)
    local.get 0 local.get 1 call_indirect (type $vector_to_i32))

  (func $check_direct (export "check_direct") (param i64) (result i64)
    local.get 0 i64x2.splat call $forward_extract)
  (func $check_indirect (export "check_indirect") (param i64) (result i64)
    local.get 0 i64x2.splat i32.const 0 call $indirect_extract)
  (func $check_v128_result (export "check_v128_result") (param i64) (result i64)
    local.get 0 i64x2.splat call $identity i64x2.extract_lane 1)
  (func $check_direct_lanes (export "check_direct_lanes") (result i32)
    v128.const i32x4 0x11223344 0x55667788 0x99aabbcc 0xddeeff00
    call $read_lane0 i32.const 0x11223344 i32.eq
    v128.const i32x4 0x11223344 0x55667788 0x99aabbcc 0xddeeff00
    call $read_lane1 i32.const 0x55667788 i32.eq i32.and
    v128.const i32x4 0x11223344 0x55667788 0x99aabbcc 0xddeeff00
    call $read_lane2 i32.const 0x99aabbcc i32.eq i32.and
    v128.const i32x4 0x11223344 0x55667788 0x99aabbcc 0xddeeff00
    call $read_lane3 i32.const 0xddeeff00 i32.eq i32.and)
  (func $check_indirect_lanes (export "check_indirect_lanes") (result i32)
    v128.const i32x4 0x11223344 0x55667788 0x99aabbcc 0xddeeff00
    i32.const 1 call $indirect_lane i32.const 0x11223344 i32.eq
    v128.const i32x4 0x11223344 0x55667788 0x99aabbcc 0xddeeff00
    i32.const 2 call $indirect_lane i32.const 0x55667788 i32.eq i32.and
    v128.const i32x4 0x11223344 0x55667788 0x99aabbcc 0xddeeff00
    i32.const 3 call $indirect_lane i32.const 0x99aabbcc i32.eq i32.and
    v128.const i32x4 0x11223344 0x55667788 0x99aabbcc 0xddeeff00
    i32.const 4 call $indirect_lane i32.const 0xddeeff00 i32.eq i32.and))
