// wasmdec: bounds=hidden
int __usercall main.b@<r0>(int a1@<r0>)
{
  int v1; // kr04_4

  v1 = wasm_global_0;
  if ( (unsigned int)wasm_global_0 <= *(_DWORD *)((char *)&unk_10 + (unsigned int)wasm_global_2) )
  {
    wasm_global_0 -= 8;
    *(_QWORD *)(unsigned int)(v1 - 8) = 4660;
    runtime.mini(0);
  }
  return 7;
}
