// wasmhex: bounds=hidden
void __usercall show_flag(int key@<r0>)
{
  unsigned int v1; // kr0C_4
  int v2; // krC0_4
  int v3; // krC0_4
  unsigned int v4; // krC0_4

  v1 = (unsigned int)_stack_pointer - 128;
  _stack_pointer = (_UNKNOWN *__ptr32)v1;
  *(_DWORD *)&byte_1C[v1 + 96] = key;
  v2 = strlen(*(_DWORD *)&byte_1C[v1 + 96]);
  *(_DWORD *)&byte_1C[v1 + 92] = v2;
  if ( unk_BC0 == 0 )
  {
    for ( *(_DWORD *)&byte_1C[v1 + 88] = 0; *(int *)&byte_1C[v1 + 88] < 17; ++*(_DWORD *)&byte_1C[v1 + 88] )
    {
      v3 = (int)*(_DWORD *)&byte_1C[v1 + 88] % 10;
      v4 = (int)((unsigned int)*(unsigned __int8 *)(unsigned int)(*(_DWORD *)&byte_1C[v1 + 96] + v3) << 0x18u);
      LOBYTE(v4) = (int)v4 >> 0x18u;
      byte_AC0[*(unsigned int *)&byte_1C[v1 + 88]] ^= v4;
    }
  }
  unk_BC0 = 1;
  *(_DWORD *)v1 = 2752;
  snprintf(v1 + 16, 100, "Good job! Your flag is: %s", v1);
  send_message(v1 + 16);
  dlfree(*(_DWORD *)&byte_1C[v1 + 96]);
  _stack_pointer = (_UNKNOWN *__ptr32)(v1 + 128);
}
