// wasmdec: bounds=hidden
void __usercall store_i32_16(int a1@<r0>, int a2@<r1>)
{
  *(_WORD *)((unsigned int)a1 + 0x2005LL) = a2;
}
