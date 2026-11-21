// wasmdec: bounds=hidden
void __usercall store_i64(int a1@<r0>, __int64 a2@<r1:r2>)
{
  *(_QWORD *)((unsigned int)a1 + 0x2001LL) = a2;
}
