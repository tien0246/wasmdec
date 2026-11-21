// wasmdec: bounds=hidden
int __usercall indirect_scalar@<r0>(int value@<r0>, int index@<r1>)
{
  switch ( index )
  {
    case 0:
    case 1:
      wasm_trap_call_indirect_type();
    case 2:
      return target_scalar(value);
    default:
      wasm_trap_call_indirect_bounds();
  }
}
