// wasmdec: bounds=hidden
// wasmdec ICF-folded with: beta (+0 more)
int __usercall alpha@<r0>(int a1@<r0>, int a2@<r1>)
{
  unsigned int v2; // kr08_4

  v2 = a1 + 4;
  ext_dummy(*(_DWORD *)((unsigned int)(a1 + 4) + 8LL));
  return *(_DWORD *)(v2 + 0xCLL);
}
