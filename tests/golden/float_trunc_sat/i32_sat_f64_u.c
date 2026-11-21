// wasmdec: bounds=hidden
int __usercall i32_sat_f64_u@<r0>(double a1@<r0:r1>)
{
  return trunc_sat_i32_u(*(unsigned __int64 *)&a1);
}
