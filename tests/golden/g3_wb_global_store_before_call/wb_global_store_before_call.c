// wasmdec: bounds=hidden
int __usercall wb_global_store_before_call@<r0>(int a1@<r0>)
{
  __int64 v1; // kr10_8
  unsigned int v2; // eax
  unsigned int v3; // ecx
  unsigned int v4; // kr28_4
  unsigned int v5; // kr04_4

  v1 = 0;
  if ( MEMORY[0x800] != 0 )
  {
    *(_QWORD *)((unsigned int)wasm_global_0 + 0x18LL) = 0;
    __SET_PAIR__(v2, v3, wasm_func_1());
    v4 = v3;
    v5 = wasm_global_0;
    *(_QWORD *)v4 = *(_QWORD *)((unsigned int)wasm_global_0 + 0x18LL);
    v1 = *(_QWORD *)(v5 + 0x18LL);
  }
  MEMORY[0x400] = v1;
  ((void (__usercall *)(__int64@<r0:r1>))wasm_func_3)(77);
  return 0;
}
