// wasmdec: bounds=hidden
int __usercall dynamic_slot@<r0>(int a1@<r0>, int a2@<r1>)
{
  switch ( a2 )
  {
    case 0:
      return add10(a1);
    case 1:
      return add20(a1);
    case 2:
      wasm_trap_call_indirect_type();
    default:
      wasm_trap_call_indirect_bounds();
  }
}
