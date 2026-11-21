// wasmdec: bounds=hidden
void __usercall wb_struct_loop_near_miss(int a1@<r0>)
{
  __int64 v1; // kr10_8
  __int64 v2; // kr18_8
  unsigned int v3; // eax
  unsigned int v4; // ecx
  unsigned int v5; // kr28_4
  __int64 v6; // [rsp+8h] [rbp-38h]
  __int64 v7; // [rsp+10h] [rbp-30h]
  __int64 v8; // [rsp+20h] [rbp-20h]

  v1 = v6;
  v2 = v8;
  if ( MEMORY[0x800] != 0 )
  {
    if ( v8 == 0 )
      wasm_func_4(0);
    v7 = *(_QWORD *)(unsigned int)v8;
    __SET_PAIR__(v3, v4, wasm_func_2());
    v5 = v4;
    *(_QWORD *)v5 = *(_QWORD *)((unsigned int)wasm_global_0 + 0x18LL);
    *(_QWORD *)(v5 + 8LL) = v7;
    v1 = v6;
    v2 = v8;
  }
  if ( v2 == 0 )
    wasm_func_4(0);
  *(_QWORD *)(unsigned int)v2 = v1;
}
