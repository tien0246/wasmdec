// wasmdec: bounds=hidden
int __usercall sat@<r0>()
{
  return trunc_sat_i32_s(COERCE_UNSIGNED_INT(INFINITY));
}
