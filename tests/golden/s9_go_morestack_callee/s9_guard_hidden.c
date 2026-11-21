// wasmdec: bounds=hidden
int __usercall s9_guard_hidden@<r0>(int a1@<r0>)
{
  if ( (unsigned int)wasm_global_0 <= *(_DWORD *)((unsigned int)wasm_global_2 + 0x10LL) )
  {
    wasm_global_0 -= 8;
    *(_DWORD *)(unsigned int)wasm_global_0 = 4660;
    wasm_func_1(0);
  }
  return 42;
}
