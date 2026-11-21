(module
  (memory i64 1)
  (func (export "init")
    i64.const 0 i64.const -35747867511423104 i64.store
    i64.const 8 i64.const -9191740941672636400 i64.store
    i64.const 512 i64.const 4914309078522313642 i64.store)
  (func (export "scalar_i32_load") (param $a i64) (result i32)
    local.get $a i32.load)
  (func (export "scalar_i64_load") (param $a i64) (result i64)
    local.get $a i64.load)
  (func (export "scalar_f32_load") (param $a i64) (result f32)
    local.get $a f32.load)
  (func (export "scalar_f64_load") (param $a i64) (result f64)
    local.get $a f64.load)
  (func (export "scalar_i32_load8_s") (param $a i64) (result i32)
    local.get $a i32.load8_s)
  (func (export "scalar_i32_load8_u") (param $a i64) (result i32)
    local.get $a i32.load8_u)
  (func (export "scalar_i32_load16_s") (param $a i64) (result i32)
    local.get $a i32.load16_s)
  (func (export "scalar_i32_load16_u") (param $a i64) (result i32)
    local.get $a i32.load16_u)
  (func (export "scalar_i64_load8_s") (param $a i64) (result i64)
    local.get $a i64.load8_s)
  (func (export "scalar_i64_load8_u") (param $a i64) (result i64)
    local.get $a i64.load8_u)
  (func (export "scalar_i64_load16_s") (param $a i64) (result i64)
    local.get $a i64.load16_s)
  (func (export "scalar_i64_load16_u") (param $a i64) (result i64)
    local.get $a i64.load16_u)
  (func (export "scalar_i64_load32_s") (param $a i64) (result i64)
    local.get $a i64.load32_s)
  (func (export "scalar_i64_load32_u") (param $a i64) (result i64)
    local.get $a i64.load32_u)
  (func (export "scalar_i32_store") (param $a i64) (param $v i32)
    local.get $a local.get $v i32.store)
  (func (export "scalar_i64_store") (param $a i64) (param $v i64)
    local.get $a local.get $v i64.store)
  (func (export "scalar_f32_store") (param $a i64) (param $v f32)
    local.get $a local.get $v f32.store)
  (func (export "scalar_f64_store") (param $a i64) (param $v f64)
    local.get $a local.get $v f64.store)
  (func (export "scalar_i32_store8") (param $a i64) (param $v i32)
    local.get $a local.get $v i32.store8)
  (func (export "scalar_i32_store16") (param $a i64) (param $v i32)
    local.get $a local.get $v i32.store16)
  (func (export "scalar_i64_store8") (param $a i64) (param $v i64)
    local.get $a local.get $v i64.store8)
  (func (export "scalar_i64_store16") (param $a i64) (param $v i64)
    local.get $a local.get $v i64.store16)
  (func (export "scalar_i64_store32") (param $a i64) (param $v i64)
    local.get $a local.get $v i64.store32)
  (func (export "simd_load") (param $a i64) (result i32)
    i64.const 4096 local.get $a v128.load v128.store
    i64.const 4096 i32.load)
  (func (export "simd_store") (param $a i64) (result i32)
    local.get $a i64.const 512 v128.load v128.store
    local.get $a i32.load)
  (func (export "widen_i8x8_s") (param $a i64) (result i32)
    i64.const 4096 local.get $a v128.load8x8_s v128.store
    i64.const 4096 i32.load)
  (func (export "widen_i8x8_u") (param $a i64) (result i32)
    i64.const 4096 local.get $a v128.load8x8_u v128.store
    i64.const 4096 i32.load)
  (func (export "widen_i16x4_s") (param $a i64) (result i32)
    i64.const 4096 local.get $a v128.load16x4_s v128.store
    i64.const 4096 i32.load)
  (func (export "widen_i16x4_u") (param $a i64) (result i32)
    i64.const 4096 local.get $a v128.load16x4_u v128.store
    i64.const 4096 i32.load)
  (func (export "widen_i32x2_s") (param $a i64) (result i64)
    i64.const 4096 local.get $a v128.load32x2_s v128.store
    i64.const 4096 i64.load)
  (func (export "widen_i32x2_u") (param $a i64) (result i64)
    i64.const 4096 local.get $a v128.load32x2_u v128.store
    i64.const 4096 i64.load)
  (func (export "splat_i8") (param $a i64) (result i32)
    i64.const 4096 local.get $a v128.load8_splat v128.store
    i64.const 4096 i32.load)
  (func (export "splat_i16") (param $a i64) (result i32)
    i64.const 4096 local.get $a v128.load16_splat v128.store
    i64.const 4096 i32.load)
  (func (export "splat_i32") (param $a i64) (result i32)
    i64.const 4096 local.get $a v128.load32_splat v128.store
    i64.const 4096 i32.load)
  (func (export "splat_i64") (param $a i64) (result i64)
    i64.const 4096 local.get $a v128.load64_splat v128.store
    i64.const 4096 i64.load)
  (func (export "zero_i32") (param $a i64) (result i32)
    i64.const 4096 local.get $a v128.load32_zero v128.store
    i64.const 4096 i32.load)
  (func (export "zero_i64") (param $a i64) (result i64)
    i64.const 4096 local.get $a v128.load64_zero v128.store
    i64.const 4096 i64.load)
  (func (export "zero_i32_high") (param $a i64) (result i64)
    i64.const 4096 local.get $a v128.load32_zero v128.store
    i64.const 4104 i64.load)
  (func (export "zero_i64_high") (param $a i64) (result i64)
    i64.const 4096 local.get $a v128.load64_zero v128.store
    i64.const 4104 i64.load)
  (func (export "lane_load8") (param $a i64) (result i32)
    i64.const 4096 local.get $a i64.const 512 v128.load v128.load8_lane 0 v128.store
    i64.const 4096 i32.load)
  (func (export "lane_store8") (param $a i64) (result i32)
    local.get $a i64.const 512 v128.load v128.store8_lane 0
    local.get $a i32.load8_u)
  (func (export "lane_load16") (param $a i64) (result i32)
    i64.const 4096 local.get $a i64.const 512 v128.load v128.load16_lane 0 v128.store
    i64.const 4096 i32.load)
  (func (export "lane_store16") (param $a i64) (result i32)
    local.get $a i64.const 512 v128.load v128.store16_lane 0
    local.get $a i32.load16_u)
  (func (export "lane_load32") (param $a i64) (result i32)
    i64.const 4096 local.get $a i64.const 512 v128.load v128.load32_lane 0 v128.store
    i64.const 4096 i32.load)
  (func (export "lane_store32") (param $a i64) (result i32)
    local.get $a i64.const 512 v128.load v128.store32_lane 0
    local.get $a i32.load)
  (func (export "lane_load64") (param $a i64) (result i64)
    i64.const 4096 local.get $a i64.const 512 v128.load v128.load64_lane 0 v128.store
    i64.const 4096 i64.load)
  (func (export "lane_store64") (param $a i64) (result i64)
    local.get $a i64.const 512 v128.load v128.store64_lane 0
    local.get $a i64.load)
  (func (export "simd_offset_u32_high") (param $a i64) (result i32)
    i64.const 4096 local.get $a v128.load offset=4294967296 v128.store
    i64.const 4096 i32.load)
  (func (export "simd_offset_u64_max") (param $a i64) (result i32)
    i64.const 4096 local.get $a v128.load offset=18446744073709551615 v128.store
    i64.const 4096 i32.load)
  (func (export "simd_offset_wrap") (param $a i64) (result i32)
    i64.const 4096 local.get $a v128.load offset=8 v128.store
    i64.const 4096 i32.load)
  (func (export "scalar_offset_u64_max") (param $a i64) (result i32)
    local.get $a i32.load8_u offset=18446744073709551615)
  (func (export "scalar_offset_wrap") (param $a i64) (result i32)
    local.get $a i32.load offset=8)
)
