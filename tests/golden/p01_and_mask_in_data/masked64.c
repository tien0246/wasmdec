// wasmdec: bounds=hidden
__int64 __usercall masked64@<r0:r1>(__int64 a1@<r0:r1>)
{
  HIDWORD(a1) = 0;
  LODWORD(a1) = (unsigned __int16)a1;
  return a1;
}
