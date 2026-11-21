// wasmdec: bounds=hidden
void __usercall bounds_paths(int a1@<r0>)
{
  unsigned __int8 v1; // kr08_1
  __int128 v2; // kr10_16
  __int64 v3; // kr08_8

  if ( a1 != 0 )
    __builtin_trap();
  v1 = 0;
  v2 = *MK_FP(1, v1);
  v3 = 65520;
  *MK_FP(1, v3) = v2;
}
