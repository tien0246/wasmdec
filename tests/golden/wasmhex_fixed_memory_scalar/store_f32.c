// wasmdec: bounds=hidden
float __usercall store_f32@<r0>(int a1@<r0>, float a2@<r1>)
{
  *(float *)((unsigned int)a1 + 0x100LL) = a2;
  return *(float *)((unsigned int)a1 + 0x100LL);
}
