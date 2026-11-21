// wasmdec: bounds=hidden
void __usercall store_i32_16(__int64 a1@<r0:r1>, int a2@<r2>)
{
  *(_WORD *)(a1 + 0x100000013LL) = a2;
}
