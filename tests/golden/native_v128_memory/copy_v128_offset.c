// wasmdec: bounds=hidden
void __usercall copy_v128_offset(int a1@<r0>, int a2@<r1>)
{
  *(_OWORD *)((unsigned int)a1 + 0x20LL) = *(_OWORD *)((unsigned int)a2 + 0x10LL);
}
