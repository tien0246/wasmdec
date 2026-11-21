// wasmdec: bounds=hidden
int __usercall splat_i16@<r0>(__int64 a1@<r0:r1>)
{
  __int128 v1; // kr40_16
  __int64 v2; // kr38_8
  __int64 v3; // kr38_8

  *(_QWORD *)&v1 = *(unsigned __int16 *)a1
                 | ((unsigned __int64)*(unsigned __int16 *)a1 << 16)
                 | ((*(unsigned __int16 *)a1 | ((unsigned __int64)*(unsigned __int16 *)a1 << 16)) << 32);
  *((_QWORD *)&v1 + 1) = v1;
  v2 = 4096;
  *MK_FP(1, v2) = v1;
  v3 = 4096;
  return *MK_FP(1, v3);
}
