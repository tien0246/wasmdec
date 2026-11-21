// wasmdec: bounds=hidden
int __usercall tail_dispatch@<r0>(int a1@<r0>, int a2@<r1>)
{
  switch ( a1 )
  {
    case 0:
      return identity(a2);
    case 1:
      wasm_trap_call_indirect_type();
    case 2:
      wasm_trap_call_indirect_null();
    case 3:
      return double(a2);
    default:
      wasm_trap_call_indirect_bounds();
  }
}
