// wasmdec: bounds=hidden
int __usercall tail_dispatch@<r0>(int index@<r0>, int value@<r1>)
{
  if ( index == 0 )
    return add_one(value);
  if ( index != 1 )
    wasm_trap_call_indirect_bounds();
  return double(value);
}
