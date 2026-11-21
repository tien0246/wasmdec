// wasmdec: bounds=hidden
void __usercall store_i64_16(__int64 a1@<r0:r1>, __int64 a2@<r2:r3>)
{
  *(_WORD *)(a1 + 0x100000015LL) = a2;
}
