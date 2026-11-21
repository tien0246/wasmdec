// wasmdec: bounds=hidden
void __usercall make_triple64(__int64 a1@<r0:r1>, void *result@<r2>)
{
  *(_QWORD *)result = a1;
  *((_QWORD *)result + 1) = 0x102030405060708LL;
  *((_QWORD *)result + 2) = 0x1020304050607LL;
}
