// wasmdec: bounds=hidden
int __usercall simd_store@<r0>(__int64 a1@<r0:r1>)
{
  __int64 v1; // kr10_8

  v1 = 512;
  *(_OWORD *)a1 = *MK_FP(1, v1);
  return *(_DWORD *)a1;
}
