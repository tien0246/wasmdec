// wasmdec: bounds=hidden
int __usercall global_index32@<r0>(int a1@<r0>)
{
  switch ( index32_1 )
  {
    case 0:
      return target0(a1);
    case 1:
      return target1(a1);
    case 2:
      return target2(a1);
    case 3:
      return target3(a1);
    default:
      wasm_trap_call_indirect_bounds();
  }
}
