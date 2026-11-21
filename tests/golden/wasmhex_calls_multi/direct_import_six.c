// wasmdec: bounds=hidden
void __usercall direct_import_six(int a1@<r0>, void *result@<r1>)
{
  int v3; // kr08_4
  int v4; // kr0C_4
  __int64 v5; // kr10_8
  int v6; // kr18_4
  int v7; // kr1C_4
  _DWORD v8[6]; // [esp+0h] [ebp-18h] BYREF

  six(a1, v8);
  v3 = v8[0];
  v5 = *(_QWORD *)&v8[1];
  v4 = v8[3];
  v6 = v8[4];
  v7 = v8[5];
  *(_DWORD *)result = v3;
  *(_QWORD *)((char *)result + 4) = v5;
  *((_DWORD *)result + 3) = v4;
  *((_DWORD *)result + 4) = v6;
  *((_DWORD *)result + 5) = v7;
}
