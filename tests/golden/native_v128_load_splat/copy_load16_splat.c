// wasmdec: bounds=hidden
void __usercall copy_load16_splat(int a1@<r0>, int a2@<r1>)
{
  __int128 v2; // kr40_16

  *(_QWORD *)&v2 = *(unsigned __int16 *)((unsigned int)a2 + 2LL)
                 | ((unsigned __int64)*(unsigned __int16 *)((unsigned int)a2 + 2LL) << 16)
                 | ((*(unsigned __int16 *)((unsigned int)a2 + 2LL)
                   | ((unsigned __int64)*(unsigned __int16 *)((unsigned int)a2 + 2LL) << 16)) << 32);
  *((_QWORD *)&v2 + 1) = v2;
  *(_OWORD *)(unsigned int)a1 = v2;
}
