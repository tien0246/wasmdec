// wasmdec: bounds=hidden
void __usercall store_f32(int a1@<r0>, float a2@<r1>)
{
  *(float *)((unsigned int)a1 + 0x2002LL) = a2;
}
