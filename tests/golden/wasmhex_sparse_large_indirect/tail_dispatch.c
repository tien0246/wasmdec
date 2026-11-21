// wasmdec: bounds=hidden
int __usercall tail_dispatch@<r0>(int a1@<r0>, int a2@<r1>)
{
  if ( a1 == 0 )
    return identity(a2);
  if ( a1 == 4096 )
    wasm_trap_call_indirect_type();
  if ( a1 != 0x1FFF )
  {
    if ( (unsigned int)a1 <= 0x1FFF )
      wasm_trap_call_indirect_null();
    wasm_trap_call_indirect_bounds();
  }
  return last(a2);
}
