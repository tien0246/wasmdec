// wasmdec: bounds=hidden
__int64 __usercall sum_triple64@<r0:r1>(__int64 a1@<r0:r1>)
{
  __int64 v1; // kr08_8
  __int64 v2; // kr10_8
  __int64 v3; // kr18_8
  __int64 v6; // kr08_8
  __int64 result; // eax:ecx
  _QWORD v8[3]; // [esp+0h] [ebp-18h] BYREF

  make_triple64(a1, v8);
  v1 = v8[0];
  v2 = v8[1];
  v3 = v8[2];
  v6 = v1 + v2 + v3;
  HIDWORD(result) = HIDWORD(v6);
  LODWORD(result) = v6;
  return result;
}
