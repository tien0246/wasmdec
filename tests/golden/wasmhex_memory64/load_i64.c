// wasmdec: bounds=hidden
__int64 __usercall load_i64@<r0:r1>(__int64 a1@<r0:r1>)
{
  __int64 v3; // kr18_8
  __int64 result; // eax:ecx

  v3 = *(_QWORD *)(a1 + 0x100000001LL);
  HIDWORD(result) = HIDWORD(v3);
  LODWORD(result) = v3;
  return result;
}
