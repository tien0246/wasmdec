// wasmhex: bounds=hidden
void __usercall check_password(int result@<r0>)
{
  int v1; // kr0C_4
  int password; // kr68_4
  int v3; // kr68_4
  int v4; // kr68_4

  v1 = (int)_stack_pointer - 32;
  _stack_pointer = (_UNKNOWN *__ptr32)v1;
  *(_DWORD *)&byte_1C[v1] = result;
  password = get_password();
  *(_DWORD *)&byte_18[v1] = password;
  *(_DWORD *)&byte_4[*(unsigned int *)&byte_1C[v1]] = *(_DWORD *)&byte_18[v1];
  v3 = strlen(*(_DWORD *)&byte_18[v1]);
  *(_DWORD *)&byte_14[v1] = v3;
  MD5_Init(&unk_BC4);
  MD5_Update(&unk_BC4, *(_DWORD *)&byte_18[v1], *(_DWORD *)&byte_14[v1]);
  MD5_Final(&unk_C60, &unk_BC4);
  hex_to_bytes("936fff76f378ace4cf83a4360cafcc20", v1);
  v4 = strcmp(&unk_C60, v1);
  *(_BYTE *)*(unsigned int *)&byte_1C[v1] = v4 == 0;
  _stack_pointer = (_UNKNOWN *__ptr32)(v1 + 32);
}
