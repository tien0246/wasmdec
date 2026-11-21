// wasmdec: bounds=hidden
__int64 __usercall i64_extend16@<r0:r1>(__int64 a1@<r0:r1>)
{
  HIDWORD(a1) = (unsigned __int64)(__int16)a1 >> 32;
  LODWORD(a1) = (__int16)a1;
  return a1;
}
