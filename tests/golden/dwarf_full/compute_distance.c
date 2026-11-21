// wasmdec: bounds=hidden
int __usercall compute_distance@<r0>(int a@<r0>, int b@<r1>)
{
  unsigned int v2; // kr10_4
  int v3; // kr54_4
  int v4; // kr70_4

  v2 = (unsigned int)_stack_pointer - 16;
  _stack_pointer = (_UNKNOWN *__ptr32)v2;
  *(_DWORD *)((char *)&unk_C + v2) = a;
  *(_DWORD *)((char *)&unk_8 + v2) = b;
  *(_DWORD *)((char *)&unk_4 + v2) = *(_DWORD *)*(unsigned int *)((char *)&unk_C + v2)
                                   - *(_DWORD *)*(unsigned int *)((char *)&unk_8 + v2);
  *(_DWORD *)v2 = *(_DWORD *)((char *)&unk_4 + *(unsigned int *)((char *)&unk_C + v2))
                - *(_DWORD *)((char *)&unk_4 + *(unsigned int *)((char *)&unk_8 + v2));
  v3 = *(_DWORD *)((char *)&unk_4 + v2) * *(_DWORD *)((char *)&unk_4 + v2) + *(_DWORD *)v2 * *(_DWORD *)v2;
  v4 = scale_static(*(_DWORD *)((char *)&unk_4 + v2), *(_DWORD *)v2);
  _stack_pointer = (_UNKNOWN *__ptr32)(v2 + 16);
  return v3 + v4;
}
