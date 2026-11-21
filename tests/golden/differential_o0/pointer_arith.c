// wasmdec: bounds=hidden
int __usercall pointer_arith@<r0>(int a1@<r0>, int a2@<r1>)
{
  unsigned int v2; // kr10_4
  int v3; // kr3C_4

  v2 = wasm_global_0 - 16;
  *(_DWORD *)((unsigned int)(wasm_global_0 - 16) + 0xCLL) = a1;
  *(_DWORD *)(v2 + 8LL) = a2;
  v3 = (int)((unsigned int)*(_DWORD *)(v2 + 8LL) << 2u);
  *(_DWORD *)(v2 + 4LL) = *(_DWORD *)(v2 + 0xCLL) + v3;
  return *(_DWORD *)*(unsigned int *)(v2 + 4LL) + *(_DWORD *)(*(unsigned int *)(v2 + 4LL) + 4LL);
}
