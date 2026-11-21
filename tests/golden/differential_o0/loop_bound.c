// wasmdec: bounds=hidden
int __usercall loop_bound@<r0>(int a1@<r0>)
{
  unsigned int v1; // kr0C_4
  int v2; // kr50_4

  v1 = wasm_global_0 - 16;
  *(_DWORD *)((unsigned int)(wasm_global_0 - 16) + 0xCLL) = a1;
  *(_DWORD *)(v1 + 8LL) = 0;
  for ( *(_DWORD *)(v1 + 4LL) = 0; *(_DWORD *)(v1 + 4LL) < *(_DWORD *)(v1 + 0xCLL); ++*(_DWORD *)(v1 + 4LL) )
  {
    v2 = (int)((unsigned int)*(_DWORD *)(v1 + 4LL) << 1u);
    *(_DWORD *)(v1 + 8LL) += v2;
  }
  return *(_DWORD *)(v1 + 8LL);
}
