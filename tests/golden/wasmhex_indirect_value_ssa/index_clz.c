// wasmdec: bounds=hidden
int __usercall index_clz@<r0>(int a1@<r0>)
{
  int v2; // kr08_4

  v2 = std::countl_zero(0x40000000);
  switch ( v2 )
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
