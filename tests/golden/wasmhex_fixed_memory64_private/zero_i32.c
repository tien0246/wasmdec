// wasmdec: bounds=hidden
int __usercall zero_i32@<r0>(__int64 a1@<r0:r1>)
{
  __int64 v1; // kr18_8
  __int64 v2; // kr18_8

  v1 = 4096;
  *MK_FP(1, v1) = *(unsigned int *)a1;
  v2 = 4096;
  return *MK_FP(1, v2);
}
