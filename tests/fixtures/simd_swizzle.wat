(module
  (func (export "swizzle_lane15") (param i32) (result i32)
    v128.const i32x4 0x03020100 0x07060504 0x0b0a0908 0x0f0e0d0c
    local.get 0
    i8x16.splat
    i8x16.swizzle
    i8x16.extract_lane_u 15))
