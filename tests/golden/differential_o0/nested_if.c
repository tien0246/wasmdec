// wasmdec: bounds=hidden
int __usercall nested_if@<r0>(int a1@<r0>, int a2@<r1>, int a3@<r2>)
{
  unsigned int v3; // kr14_4

  v3 = wasm_global_0 - 16;
  *(_DWORD *)((unsigned int)(wasm_global_0 - 16) + 8LL) = a1;
  *(_DWORD *)(v3 + 4LL) = a2;
  *(_DWORD *)v3 = a3;
  if ( *(int *)(v3 + 8LL) <= 0 )
  {
    *(_DWORD *)(v3 + 0xCLL) = 4;
  }
  else if ( *(int *)(v3 + 4LL) <= 0 )
  {
    *(_DWORD *)(v3 + 0xCLL) = 3;
  }
  else if ( *(int *)v3 <= 0 )
  {
    *(_DWORD *)(v3 + 0xCLL) = 2;
  }
  else
  {
    *(_DWORD *)(v3 + 0xCLL) = 1;
  }
  return *(_DWORD *)(v3 + 0xCLL);
}
