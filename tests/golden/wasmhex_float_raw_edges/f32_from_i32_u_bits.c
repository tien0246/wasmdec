// wasmdec: bounds=hidden
int __usercall f32_from_i32_u_bits@<r0>(int a1@<r0>)
{
  return (int)LODWORD((float)(unsigned int)a1);
}
