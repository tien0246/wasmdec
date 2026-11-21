// wasmdec: bounds=hidden
int __usercall load8x8_u@<r0>(int a1@<r0>)
{
  __int128 v1; // kr110_16
  __int64 v2; // kr08_8
  __int64 v3; // kr08_8
  bool v4; // kr04_1
  __int64 v5; // kr08_8

  *(_QWORD *)&v1 = (unsigned __int8)*(_QWORD *)((unsigned int)a1 + 1LL)
                 | ((unsigned __int64)(unsigned __int8)BYTE1(*(_QWORD *)((unsigned int)a1 + 1LL)) << 16)
                 | ((unsigned __int64)(unsigned __int8)BYTE2(*(_QWORD *)((unsigned int)a1 + 1LL)) << 32)
                 | ((unsigned __int64)BYTE3(*(_QWORD *)((unsigned int)a1 + 1LL)) << 48);
  *((_QWORD *)&v1 + 1) = (unsigned __int8)BYTE4(*(_QWORD *)((unsigned int)a1 + 1LL))
                       | ((unsigned __int64)(unsigned __int8)((unsigned __int16)WORD2(*(_QWORD *)((unsigned int)a1 + 1LL)) >> 8) << 16)
                       | ((unsigned __int64)(unsigned __int8)BYTE6(*(_QWORD *)((unsigned int)a1 + 1LL)) << 32)
                       | ((unsigned __int64)(unsigned __int8)HIBYTE(*(_QWORD *)((unsigned int)a1 + 1LL)) << 48);
  v2 = 512;
  *MK_FP(1, v2) = v1;
  v3 = 512;
  v4 = *MK_FP(1, v3) == 0xFF007F00010080LL;
  v5 = 520;
  return v4 & (*MK_FP(1, v5) == 0x80000000120034LL);
}
