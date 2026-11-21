// wasmdec: bounds=hidden
int __usercall widen_i16x4_s@<r0>(__int64 a1@<r0:r1>)
{
  __int128 v1; // kr90_16
  __int64 v2; // krA0_8
  __int64 v3; // krA0_8

  *(_QWORD *)&v1 = (unsigned int)(__int16)*(_QWORD *)a1
                 | ((unsigned __int64)(unsigned int)(__int16)WORD1(*(_QWORD *)a1) << 32);
  *((_QWORD *)&v1 + 1) = (unsigned int)(__int16)WORD2(*(_QWORD *)a1)
                       | ((unsigned __int64)(unsigned int)(__int16)HIWORD(*(_QWORD *)a1) << 32);
  v2 = 4096;
  *MK_FP(1, v2) = v1;
  v3 = 4096;
  return *MK_FP(1, v3);
}
