// wasmdec: bounds=hidden
int __usercall dispatch@<r0>(int a1@<r0>, int a2@<r1>, int a3@<r2>)
{
  if ( a1 == 0 )
    return wasm_func_0(a2, a3);
  if ( a1 != 1 )
    wasm_trap_call_indirect_bounds();
  return wasm_func_1(a2, a3);
}
