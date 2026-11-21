// wasmdec: bounds=hidden
void __usercall store_i64_8(int a1@<r0>, __int64 a2@<r1:r2>)
{
  *(_BYTE *)((unsigned int)a1 + 0x2006LL) = a2;
}
