// wasmdec: bounds=hidden
__int64 __usercall store_fill_load@<r0:r1>(int a1@<r0>)
{
  __int64 result; // eax:ecx

  *(_QWORD *)(unsigned int)a1 = 0x1122334455667788LL;
  memset((unsigned int)a1, 0, 0x40u);
  HIDWORD(result) = HIDWORD(*(_QWORD *)(unsigned int)a1);
  LODWORD(result) = *(_QWORD *)(unsigned int)a1;
  return result;
}
