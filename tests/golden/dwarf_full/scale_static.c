// wasmdec: bounds=hidden
int __usercall scale_static@<r0>(int factor@<r0>, int base@<r1>)
{
  unsigned int v2; // kr10_4

  v2 = (unsigned int)_stack_pointer - 16;
  *(_DWORD *)((char *)&unk_C + (unsigned int)_stack_pointer - 16) = factor;
  *(_DWORD *)((char *)&unk_8 + v2) = base;
  *(_DWORD *)((char *)&unk_4 + v2) = *(_DWORD *)((char *)&unk_C + v2) * *(_DWORD *)((char *)&unk_8 + v2);
  return *(_DWORD *)((char *)&unk_4 + v2) + data;
}
