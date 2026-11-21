// wasmdec: bounds=hidden
void __usercall forward_six_words(void *result@<r0>)
{
  int v2; // kr04_4
  __int64 v3; // kr08_8
  __int64 v4; // kr10_8
  int v5; // kr18_4
  _DWORD v6[6]; // [esp+0h] [ebp-18h] BYREF

  make_six_words(v6);
  v2 = v6[0];
  v3 = *(_QWORD *)&v6[1];
  v4 = *(_QWORD *)&v6[3];
  v5 = v6[5];
  *(_DWORD *)result = v2;
  *(_QWORD *)((char *)result + 4) = v3;
  *(_QWORD *)((char *)result + 12) = v4;
  *((_DWORD *)result + 5) = v5;
}
