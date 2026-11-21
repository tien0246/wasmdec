// wasmdec: bounds=hidden
int __usercall test@<r0>()
{
  float v0; // kr0C_4

  v0 = 1.0 < 2.0 ? 3.0 : 4.0;
  return wasm_trap_i32_trunc_f32_s(v0);
}
