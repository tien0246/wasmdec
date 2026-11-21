// wasmdec: bounds=hidden
int __usercall s9_infix_callee_kept@<r0>(int a1@<r0>)
{
  if ( (unsigned int)wasm_global_0 <= *(_DWORD *)((unsigned int)wasm_global_2 + 0x10LL) )
  {
    wasm_global_0 -= 8;
    *(_DWORD *)(unsigned int)wasm_global_0 = 4660;
    xwasm_func_9(0);
  }
  return 9;
}
