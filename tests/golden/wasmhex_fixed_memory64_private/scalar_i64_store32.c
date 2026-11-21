// wasmdec: bounds=hidden
void __usercall scalar_i64_store32(__int64 a1@<r0:r1>, __int64 a2@<r2:r3>)
{
  *(_DWORD *)a1 = a2;
}
