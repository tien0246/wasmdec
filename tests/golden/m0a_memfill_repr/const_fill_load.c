// wasmdec: bounds=hidden
__int64 __usercall const_fill_load@<r0:r1>()
{
  unsigned __int64 v0; // kr08_8
  __int64 v1; // kr08_8
  __int64 result; // eax:ecx

  v0 = 4096;
  memset(v0, 0x33u, 0x40u);
  v1 = 4104;
  HIDWORD(result) = HIDWORD(*MK_FP(1, v1));
  LODWORD(result) = *MK_FP(1, v1);
  return result;
}
