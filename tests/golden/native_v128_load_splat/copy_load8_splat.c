// wasmdec: bounds=hidden
void __usercall copy_load8_splat(int a1@<r0>, int a2@<r1>)
{
  __int128 v2; // kr50_16

  *(_QWORD *)&v2 = *(unsigned __int8 *)((unsigned int)a2 + 1LL)
                 | ((unsigned __int64)*(unsigned __int8 *)((unsigned int)a2 + 1LL) << 8)
                 | ((*(unsigned __int8 *)((unsigned int)a2 + 1LL)
                   | ((unsigned __int64)*(unsigned __int8 *)((unsigned int)a2 + 1LL) << 8)) << 16)
                 | ((*(unsigned __int8 *)((unsigned int)a2 + 1LL)
                   | ((unsigned __int64)*(unsigned __int8 *)((unsigned int)a2 + 1LL) << 8)
                   | ((*(unsigned __int8 *)((unsigned int)a2 + 1LL)
                     | ((unsigned __int64)*(unsigned __int8 *)((unsigned int)a2 + 1LL) << 8)) << 16)) << 32);
  *((_QWORD *)&v2 + 1) = v2;
  *(_OWORD *)(unsigned int)a1 = v2;
}
