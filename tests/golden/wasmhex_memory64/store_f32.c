// wasmdec: bounds=hidden
void __usercall store_f32(__int64 a1@<r0:r1>, float a2@<r2>)
{
  *(float *)(a1 + 0x100000010LL) = a2;
}
