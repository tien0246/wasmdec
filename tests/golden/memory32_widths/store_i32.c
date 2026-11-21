// wasmdec: bounds=hidden
void __usercall store_i32(int a1@<r0>, int a2@<r1>)
{
  *(_DWORD *)((unsigned int)a1 + 0x2000LL) = a2;
}
