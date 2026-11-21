// wasmdec: bounds=hidden
int __usercall i32_trunc_f32_u_bits@<r0>(int a1@<r0>)
{
  float v1; // kr08_4

  v1 = COERCE_FLOAT((unsigned int)a1);
  return wasm_trap_i32_trunc_f32_u(v1);
}
