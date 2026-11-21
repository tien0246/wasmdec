// wasmdec: bounds=hidden
void __usercall init()
{
  __int64 v0; // kr00_8
  __int64 v1; // kr00_8
  __int64 v2; // kr00_8

  v0 = 128;
  *MK_FP(1, v0) = 0x12345678EFCDAB89LL;
  v1 = 160;
  *MK_FP(1, v1) = 1.5;
  v2 = 168;
  *MK_FP(1, v2) = 2.25;
}
