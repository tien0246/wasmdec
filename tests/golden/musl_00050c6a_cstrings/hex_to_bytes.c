// wasmhex: bounds=hidden
void __usercall hex_to_bytes(int hexstring@<r0>, int result@<r1>)
{
  unsigned int v2; // kr10_4
  int v3; // kr34_4

  v2 = (unsigned int)_stack_pointer - 16;
  _stack_pointer = (_UNKNOWN *__ptr32)v2;
  *(_DWORD *)&byte_C[v2] = hexstring;
  *(_DWORD *)&byte_8[v2] = result;
  for ( *(_DWORD *)&byte_4[v2] = 0; *(int *)&byte_4[v2] < 16; ++*(_DWORD *)&byte_4[v2] )
  {
    v3 = *(_DWORD *)&byte_C[v2];
    *(_DWORD *)v2 = *(_DWORD *)&byte_8[v2] + *(_DWORD *)&byte_4[v2];
    sscanf(v3, "%2hhx", v2);
    *(_DWORD *)&byte_C[v2] += 2;
  }
  _stack_pointer = (_UNKNOWN *__ptr32)(v2 + 16);
}
