// wasmdec: bounds=hidden
int __usercall factorial@<r0>(int a1@<r0>)
{
  unsigned int v1; // kr0C_4
  int v2; // kr28_4
  int v3; // kr4C_4
  int v4; // kr40_4

  v1 = wasm_global_0 - 16;
  wasm_global_0 = v1;
  *(_DWORD *)(v1 + 8LL) = a1;
  if ( *(int *)(v1 + 8LL) > 1 )
  {
    v2 = *(_DWORD *)(v1 + 8LL);
    v3 = factorial(v2 - 1);
    *(_DWORD *)(v1 + 0xCLL) = v2 * v3;
  }
  else
  {
    *(_DWORD *)(v1 + 0xCLL) = 1;
  }
  v4 = *(_DWORD *)(v1 + 0xCLL);
  wasm_global_0 = v1 + 16;
  return v4;
}
