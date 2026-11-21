// wasmdec: bounds=hidden
void __usercall store_i32_8(int a1@<r0>, int a2@<r1>)
{
  *(_BYTE *)((unsigned int)a1 + 0x2004LL) = a2;
}
