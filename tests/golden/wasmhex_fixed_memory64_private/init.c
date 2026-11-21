// wasmdec: bounds=hidden
void __usercall init()
{
  unsigned __int8 v0; // kr00_1
  __int64 v1; // kr00_8
  __int64 v2; // kr00_8

  v0 = 0;
  *MK_FP(1, v0) = 0xFF80FF80FF80FF80LL;
  v1 = 8;
  *MK_FP(1, v1) = 0x8070605040302010LL;
  v2 = 512;
  *MK_FP(1, v2) = 0x44332211DDCCBBAALL;
}
