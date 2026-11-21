// wasmdec: bounds=hidden
void __usercall store_i64_32(__int64 a1@<r0:r1>, __int64 a2@<r2:r3>)
{
  *(_DWORD *)(a1 + 0x100000016LL) = a2;
}
