// wasmdec: bounds=hidden
__int64 __usercall read_i64@<r0:r1>(__int64 a1@<r0:r1>)
{
  __int64 v3; // kr10_8
  __int64 result; // eax:ecx

  v3 = *(_QWORD *)a1;
  HIDWORD(result) = HIDWORD(*(_QWORD *)a1);
  LODWORD(result) = v3;
  return result;
}
