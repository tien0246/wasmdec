// wasmdec: bounds=hidden
void __usercall store_i32_8(__int64 a1@<r0:r1>, int a2@<r2>)
{
  *(_BYTE *)(a1 + 0x100000012LL) = a2;
}
