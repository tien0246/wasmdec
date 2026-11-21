// wasmdec: bounds=hidden
int __usercall hex2@<r0>(int a1@<r0>, int a2@<r1>)
{
  unsigned int v2; // kr08_4
  int v3; // kr10_4

  _stack_pointer -= 16;
  v2 = _stack_pointer;
  *(_DWORD *)((char *)&unk_C + (unsigned int)_stack_pointer) = a1;
  v3 = sscanf(*(_DWORD *)((char *)&unk_C + v2), "%2x", v2);
  _stack_pointer = v2 + 16;
  return v3;
}
