// wasmdec: bounds=hidden
int __usercall a3_no_morestack_kept@<r0>(int a1@<r0>)
{
  if ( *(_DWORD *)(unsigned int)(_stack_pointer + 16) != 0 )
    wasm_global_1 = 5;
  return 3;
}
