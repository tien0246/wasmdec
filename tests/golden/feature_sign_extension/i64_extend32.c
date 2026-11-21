// wasmdec: bounds=hidden
__int64 __usercall i64_extend32@<r0:r1>(__int64 a1@<r0:r1>)
{
  HIDWORD(a1) = (int)a1 >> 31;
  return a1;
}
