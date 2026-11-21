// wasmdec: bounds=hidden
__int64 __usercall wasm_func_6@<r0:r1>(__int64 a1@<r0:r1>)
{
  __int64 v1; // kr10_8
  __int64 v4; // kr18_8
  __int64 result; // eax:ecx

  v1 = *(_QWORD *)((char *)&unk_A0 + (unsigned int)*(_QWORD *)((char *)&unk_30 + (unsigned int)wasm_global_2));
  if ( (unsigned __int64)(*(_QWORD *)((char *)&unk_B8 + (unsigned int)v1) + a1) > *(_QWORD *)((char *)&unk_C0
                                                                                            + (unsigned int)v1) )
    __builtin_trap();
  v4 = *(_QWORD *)((char *)&unk_B8 + (unsigned int)v1) + a1;
  HIDWORD(result) = HIDWORD(v4);
  LODWORD(result) = v4;
  return result;
}
