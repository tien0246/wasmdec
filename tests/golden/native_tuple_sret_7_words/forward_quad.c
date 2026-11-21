// wasmdec: bounds=hidden
void __usercall forward_quad(void *result@<r0>)
{
  int v2; // kr04_4
  __int64 v3; // kr08_8
  __int64 v4; // kr10_8
  __int64 v5; // kr18_8
  _QWORD v6[4]; // [esp+0h] [ebp-20h] BYREF

  make_quad(v6);
  v3 = v6[0];
  v4 = v6[1];
  v5 = v6[2];
  v2 = v6[3];
  *(_QWORD *)result = v3;
  *((_QWORD *)result + 1) = v4;
  *((_QWORD *)result + 2) = v5;
  *((_DWORD *)result + 6) = v2;
}
