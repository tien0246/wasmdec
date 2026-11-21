// wasmdec: bounds=hidden
void __usercall copy_load32_splat(int a1@<r0>, int a2@<r1>)
{
  __int128 v2; // kr30_16

  *(_QWORD *)&v2 = *(unsigned int *)((unsigned int)a2 + 4LL)
                 | ((unsigned __int64)*(unsigned int *)((unsigned int)a2 + 4LL) << 32);
  *((_QWORD *)&v2 + 1) = v2;
  *(_OWORD *)(unsigned int)a1 = v2;
}
