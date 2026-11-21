// wasmdec: bounds=hidden
int __usercall switch_dispatch@<r0>(int a1@<r0>, int a2@<r1>)
{
  unsigned int v2; // kr10_4

  v2 = wasm_global_0 - 16;
  *(_DWORD *)((unsigned int)(wasm_global_0 - 16) + 8LL) = a1;
  *(_DWORD *)(v2 + 4LL) = a2;
  switch ( *(_DWORD *)(v2 + 8LL) )
  {
    case 1:
      *(_DWORD *)(v2 + 0xCLL) = *(_DWORD *)(v2 + 4LL) + 10;
      break;
    case 2:
      *(_DWORD *)(v2 + 0xCLL) = *(_DWORD *)(v2 + 4LL) + 20;
      break;
    case 3:
      *(_DWORD *)(v2 + 0xCLL) = *(_DWORD *)(v2 + 4LL) + 30;
      break;
    default:
      *(_DWORD *)(v2 + 0xCLL) = -1;
      break;
  }
  return *(_DWORD *)(v2 + 0xCLL);
}
