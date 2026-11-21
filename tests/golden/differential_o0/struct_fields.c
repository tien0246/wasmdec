// wasmdec: bounds=hidden
int __usercall struct_fields@<r0>(int a1@<r0>)
{
  unsigned int v1; // kr0C_4
  double v2; // kr28_8
  unsigned int v3; // eax
  unsigned int v4; // ecx
  double v5; // kr70_8
  int v6; // kr5C_4
  unsigned int v7; // kr4C_4

  v1 = wasm_global_0 - 16;
  *(_DWORD *)((unsigned int)(wasm_global_0 - 16) + 0xCLL) = a1;
  v2 = *(double *)(*(unsigned int *)(v1 + 0xCLL) + 8LL);
  __SET_PAIR__(v3, v4, fabs(*(unsigned __int64 *)&v2));
  v5 = COERCE_DOUBLE(__PAIR64__(v3, v4));
  if ( v5 >= 2147483648.0 )
    v7 = 0x80000000;
  else
    v7 = wasm_trap_i32_trunc_f64_s(v2);
  return *(_DWORD *)*(unsigned int *)(v1 + 0xCLL) + *(_DWORD *)(*(unsigned int *)(v1 + 0xCLL) + 0x18LL) + v7;
}
