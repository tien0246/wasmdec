// wasmdec: bounds=hidden
int __usercall i32_sat_s@<r0>(float a1@<r0>)
{
  return trunc_sat_i32_s(LODWORD(a1));
}
