// wasmdec: bounds=hidden
__int64 __usercall fill_load_same_window@<r0:r1>(int a1@<r0>)
{
  __int64 v1; // kr10_8
  __int64 result; // eax:ecx

  v1 = (unsigned int)a1;
  memset((unsigned int)a1, 0x5Au, 0x10u);
  HIDWORD(result) = HIDWORD(*(_QWORD *)(v1 + 8));
  LODWORD(result) = *(_QWORD *)(v1 + 8);
  return result;
}
