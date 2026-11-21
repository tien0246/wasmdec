// wasmdec: bounds=hidden
int __usercall struct_fields@<r0>(int a1@<r0>)
{
  int v1; // kr04_4
  double v2; // kr08_8
  unsigned int v3; // eax
  unsigned int v4; // ecx
  double v5; // kr28_8
  int v6; // kr14_4

  v1 = *(_DWORD *)((unsigned int)a1 + 0x18LL) + *(_DWORD *)(unsigned int)a1;
  v2 = *(double *)((unsigned int)a1 + 8LL);
  __SET_PAIR__(v3, v4, fabs(*(unsigned __int64 *)&v2));
  v5 = COERCE_DOUBLE(__PAIR64__(v3, v4));
  if ( v5 >= 2147483648.0 )
    return v1 + 0x80000000;
  v6 = wasm_trap_i32_trunc_f64_s(v2);
  return v1 + v6;
}
