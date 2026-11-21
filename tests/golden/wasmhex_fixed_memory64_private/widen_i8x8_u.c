// wasmdec: bounds=hidden
int __usercall widen_i8x8_u@<r0>(__int64 a1@<r0:r1>)
{
  __int128 v1; // kr110_16
  __int64 v2; // kr120_8
  __int64 v3; // kr120_8

  *(_QWORD *)&v1 = (unsigned __int8)*(_QWORD *)a1
                 | ((unsigned __int64)(unsigned __int8)BYTE1(*(_QWORD *)a1) << 16)
                 | ((unsigned __int64)(unsigned __int8)BYTE2(*(_QWORD *)a1) << 32)
                 | ((unsigned __int64)BYTE3(*(_QWORD *)a1) << 48);
  *((_QWORD *)&v1 + 1) = (unsigned __int8)BYTE4(*(_QWORD *)a1)
                       | ((unsigned __int64)(unsigned __int8)((unsigned __int16)WORD2(*(_QWORD *)a1) >> 8) << 16)
                       | ((unsigned __int64)(unsigned __int8)BYTE6(*(_QWORD *)a1) << 32)
                       | ((unsigned __int64)(unsigned __int8)HIBYTE(*(_QWORD *)a1) << 48);
  v2 = 4096;
  *MK_FP(1, v2) = v1;
  v3 = 4096;
  return *MK_FP(1, v3);
}
