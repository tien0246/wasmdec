// wasmdec: bounds=hidden
int __usercall f32_div_bits@<r0>(int a1@<r0>, int a2@<r1>)
{
  float v2; // kr0C_4
  float v3; // kr10_4

  v2 = COERCE_FLOAT((unsigned int)a1);
  v3 = COERCE_FLOAT((unsigned int)a2);
  return wasm_i32_reinterpret_f32_bits(v2 / v3);
}
