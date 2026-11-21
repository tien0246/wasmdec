// wasmdec: bounds=hidden
void __usercall store_i64_16(int a1@<r0>, __int64 a2@<r1:r2>)
{
  *(_WORD *)((unsigned int)a1 + 0x2007LL) = a2;
}
