// wasmdec: bounds=hidden
int __usercall index_div_zero@<r0>(int a1@<r0>)
{
  unsigned int v2; // kr08_4

  v2 = 1u / (unsigned int)0;
  switch ( v2 )
  {
    case 0u:
      return target0(a1);
    case 1u:
      return target1(a1);
    case 2u:
      return target2(a1);
    case 3u:
      return target3(a1);
    default:
      wasm_trap_call_indirect_bounds();
  }
}
