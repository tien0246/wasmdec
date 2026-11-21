// wasmdec: bounds=hidden
void __usercall dispatch(int a1@<r0>, void *result@<r1>)
{
  __int64 v3; // kr08_8
  __int64 v4; // kr10_8
  __int64 v5; // kr18_8
  int v6; // kr20_4
  _QWORD v7[4]; // [esp+0h] [ebp-20h] BYREF

  if ( a1 != 0 )
  {
    if ( a1 != 1 )
      wasm_trap_call_indirect_bounds();
    target_b(v7);
    v3 = v7[0];
    v4 = v7[1];
    v5 = v7[2];
    v6 = v7[3];
  }
  else
  {
    target_a(v7);
    v3 = v7[0];
    v4 = v7[1];
    v5 = v7[2];
    v6 = v7[3];
  }
  *(_QWORD *)result = v3;
  *((_QWORD *)result + 1) = v4;
  *((_QWORD *)result + 2) = v5;
  *((_DWORD *)result + 6) = v6;
}
