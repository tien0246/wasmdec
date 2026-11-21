// wasmdec: bounds=hidden
int __usercall call_two_levels@<r0>(int a1@<r0>)
{
  unsigned int v1; // kr0C_4
  int v2; // kr30_4

  v1 = wasm_global_0 - 16;
  wasm_global_0 = v1;
  *(_DWORD *)(v1 + 0xCLL) = a1;
  v2 = wasm_func_6(*(_DWORD *)(v1 + 0xCLL));
  wasm_global_0 = v1 + 16;
  return 3 * v2 + 4;
}
