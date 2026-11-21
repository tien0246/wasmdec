// wasmdec: bounds=hidden
int __usercall simd_offset_u64_max@<r0>(__int64 a1@<r0:r1>)
{
  __int64 v1; // kr10_8
  __int64 v2; // kr10_8

  v1 = 4096;
  *MK_FP(1, v1) = *(_OWORD *)(a1 - 1);
  v2 = 4096;
  return *MK_FP(1, v2);
}
