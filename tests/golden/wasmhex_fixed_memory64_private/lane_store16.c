// wasmdec: bounds=hidden
int __usercall lane_store16@<r0>(__int64 a1@<r0:r1>)
{
  __int64 v1; // kr10_8

  v1 = 512;
  *(_WORD *)a1 = *MK_FP(1, v1);
  return *(unsigned __int16 *)a1;
}
