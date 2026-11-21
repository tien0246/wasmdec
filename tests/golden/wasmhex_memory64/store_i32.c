// wasmdec: bounds=hidden
void __usercall store_i32(__int64 a1@<r0:r1>, int a2@<r2>)
{
  *(_DWORD *)(a1 + 0x10000000ELL) = a2;
}
