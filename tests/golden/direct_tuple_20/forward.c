// wasmdec: bounds=hidden
void __usercall forward(void *result@<r0>)
{
  int v2; // kr04_4
  __int64 v3; // kr08_8
  int v4; // kr10_4
  int v5; // kr14_4
  _DWORD v6[6]; // [esp+0h] [ebp-18h] BYREF

  make(v6);
  v2 = v6[0];
  v3 = *(_QWORD *)&v6[1];
  v4 = v6[3];
  v5 = v6[4];
  *(_DWORD *)result = v2;
  *(_QWORD *)((char *)result + 4) = v3;
  *((_DWORD *)result + 3) = v4;
  *((_DWORD *)result + 4) = v5;
}
