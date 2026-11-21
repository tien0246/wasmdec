// wasmdec: bounds=hidden
int __usercall f32_from_i64_u_bits@<r0>(__int64 a1@<r0:r1>)
{
  return (int)LODWORD((float)(unsigned __int64)a1);
}
