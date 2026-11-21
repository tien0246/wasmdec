// wasmdec: bounds=hidden
void __usercall store_i64_32(int a1@<r0>, __int64 a2@<r1:r2>)
{
  *(_DWORD *)((unsigned int)a1 + 0x2008LL) = a2;
}
