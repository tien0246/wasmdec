// wasmdec: bounds=hidden
int __usercall a1_first_arg_is_temp@<r0>(int a1@<r0>, int a2@<r1>)
{
  if ( a2 == 0 )
    return wasm_func_1(unk_400, 0);
  if ( a2 != 1 )
    wasm_trap_call_indirect_bounds();
  return wasm_func_2(unk_400, 1);
}
