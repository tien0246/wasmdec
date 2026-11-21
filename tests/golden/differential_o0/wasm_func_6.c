// wasmdec: bounds=hidden
int __usercall wasm_func_6@<r0>(int a1@<r0>)
{
  unsigned int v1; // kr0C_4

  v1 = wasm_global_0 - 16;
  *(_DWORD *)((unsigned int)(wasm_global_0 - 16) + 0xCLL) = a1;
  return 3 * *(_DWORD *)(v1 + 0xCLL);
}
