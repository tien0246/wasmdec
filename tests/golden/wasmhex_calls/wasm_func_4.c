// wasmdec: bounds=hidden
void __usercall wasm_func_4(int a1@<r0>, void *result@<r1>)
{
  *(_DWORD *)result = a1;
  *(_QWORD *)((char *)result + 4) = (unsigned int)a1;
  *((_DWORD *)result + 3) = 11;
  *((_DWORD *)result + 4) = 22;
  *((_DWORD *)result + 5) = 33;
}
