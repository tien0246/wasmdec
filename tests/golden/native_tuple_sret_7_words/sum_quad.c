// wasmdec: bounds=hidden
__int64 __usercall sum_quad@<r0:r1>()
{
  __int64 v0; // kr00_8
  __int64 v1; // kr08_8
  __int64 v2; // kr10_8
  unsigned int v3; // kr18_4
  __int64 v6; // kr00_8
  __int64 result; // eax:ecx
  _QWORD v8[4]; // [esp+0h] [ebp-20h] BYREF

  make_quad(v8);
  v0 = v8[0];
  v1 = v8[1];
  v2 = v8[2];
  v3 = v8[3];
  v6 = v0 + v1 + v2 + v3;
  HIDWORD(result) = HIDWORD(v6);
  LODWORD(result) = v6;
  return result;
}
