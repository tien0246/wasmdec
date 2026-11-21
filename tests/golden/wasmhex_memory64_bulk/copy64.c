// wasmdec: bounds=hidden
void __usercall copy64(__int64 a1@<r0:r1>, __int64 a2@<r2:r3>, __int64 a3@<r6:r7>)
{
  memmove(a1, a2, a3);
}
