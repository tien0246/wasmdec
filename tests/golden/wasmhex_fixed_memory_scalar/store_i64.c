// wasmdec: bounds=hidden
__int64 __usercall store_i64@<r0:r1>(int a1@<r0>, __int64 a2@<r1:r2>)
{
  __int64 v4; // kr18_8
  __int64 result; // eax:ecx

  *(_QWORD *)((unsigned int)a1 + 0x100LL) = a2;
  v4 = *(_QWORD *)((unsigned int)a1 + 0x100LL);
  HIDWORD(result) = HIDWORD(v4);
  LODWORD(result) = v4;
  return result;
}
