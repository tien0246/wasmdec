// wasmdec: bounds=hidden
void __usercall store_i64_8(__int64 a1@<r0:r1>, __int64 a2@<r2:r3>)
{
  *(_BYTE *)(a1 + 0x100000014LL) = a2;
}
