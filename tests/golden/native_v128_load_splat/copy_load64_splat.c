// wasmdec: bounds=hidden
void __usercall copy_load64_splat(int a1@<r0>, int a2@<r1>)
{
  __int128 v2; // kr20_16

  *(_QWORD *)&v2 = *(_QWORD *)((unsigned int)a2 + 8LL);
  *((_QWORD *)&v2 + 1) = v2;
  *(_OWORD *)(unsigned int)a1 = v2;
}
