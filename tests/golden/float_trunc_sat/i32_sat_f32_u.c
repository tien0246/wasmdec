// wasmdec: bounds=hidden
int __usercall i32_sat_f32_u@<r0>(float a1@<r0>)
{
  return trunc_sat_i32_u(LODWORD(a1));
}
