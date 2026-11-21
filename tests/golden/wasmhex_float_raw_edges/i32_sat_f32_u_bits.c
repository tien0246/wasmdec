// wasmdec: bounds=hidden
int __usercall i32_sat_f32_u_bits@<r0>(int a1@<r0>)
{
  unsigned int v1; // kr08_4

  *(float *)&v1 = COERCE_FLOAT((unsigned int)a1);
  return trunc_sat_i32_u(v1);
}
