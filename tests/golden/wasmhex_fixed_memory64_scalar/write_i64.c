// wasmdec: bounds=hidden
void __usercall write_i64(__int64 a1@<r0:r1>, __int64 a2@<r2:r3>)
{
  *(_QWORD *)a1 = a2;
}
