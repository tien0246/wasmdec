// wasmdec: bounds=hidden
int __usercall runtime_systemstack@<r0>(int a1@<r0>)
{
  int v1; // kr04_4
  __int64 v2; // kr08_8

  v1 = wasm_global_0;
  v2 = *(_QWORD *)((unsigned int)wasm_global_0 + 8LL);
  if ( wasm_global_2 == *(_QWORD *)(unsigned int)*(_QWORD *)((unsigned int)wasm_global_2 + 0x30LL) )
  {
    wasm_global_1 = *(_QWORD *)((unsigned int)wasm_global_0 + 8LL);
    return (*(int (__usercall **)@<r0>())(unsigned int)v2)();
  }
  else
  {
    wasm_global_2 = *(_QWORD *)(unsigned int)*(_QWORD *)((unsigned int)wasm_global_2 + 0x30LL);
    wasm_global_1 = v2;
    (*(void (__usercall **)())(unsigned int)v2)();
    wasm_global_0 = v1 + 8;
    return 0;
  }
}
