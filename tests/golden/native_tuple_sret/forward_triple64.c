// wasmdec: bounds=hidden
void __usercall forward_triple64(__int64 a1@<r0:r1>, void *result@<r2>)
{
  __int64 v3; // kr10_8
  __int64 v4; // kr18_8
  __int64 v5; // kr20_8
  _QWORD v6[3]; // [esp+0h] [ebp-18h] BYREF

  make_triple64(a1, v6);
  v3 = v6[0];
  v4 = v6[1];
  v5 = v6[2];
  *(_QWORD *)result = v3;
  *((_QWORD *)result + 1) = v4;
  *((_QWORD *)result + 2) = v5;
}
