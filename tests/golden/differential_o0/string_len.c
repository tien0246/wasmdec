// wasmdec: bounds=hidden
int __usercall string_len@<r0>(int a1@<r0>)
{
  unsigned int v1; // kr0C_4
  unsigned int v2; // kr40_4
  int v3; // kr40_4

  v1 = wasm_global_0 - 16;
  *(_DWORD *)((unsigned int)(wasm_global_0 - 16) + 0xCLL) = a1;
  for ( *(_DWORD *)(v1 + 8LL) = 0; ; ++*(_DWORD *)(v1 + 8LL) )
  {
    v2 = (int)((unsigned int)*(unsigned __int8 *)(unsigned int)(*(_DWORD *)(v1 + 0xCLL) + *(_DWORD *)(v1 + 8LL)) << 0x18u);
    v3 = (int)v2 >> 0x18u;
    if ( v3 == 0 )
      break;
  }
  return *(_DWORD *)(v1 + 8LL);
}
