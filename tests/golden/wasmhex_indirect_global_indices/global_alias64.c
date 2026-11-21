// wasmdec: bounds=hidden
int __usercall global_alias64@<r0>(int a1@<r0>)
{
  switch ( index64_alias )
  {
    case 0LL:
      return target0(a1);
    case 1LL:
      return target1(a1);
    case 2LL:
      return target2(a1);
    case 3LL:
      return target3(a1);
    default:
      wasm_trap_call_indirect_bounds();
  }
}
