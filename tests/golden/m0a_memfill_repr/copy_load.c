// wasmdec: bounds=hidden
__int64 __usercall copy_load@<r0:r1>(int a1@<r0>, int a2@<r1>)
{
  __int64 result; // eax:ecx

  memmove((unsigned int)a1, (unsigned int)a2, 0x20u);
  HIDWORD(result) = HIDWORD(*(_QWORD *)((unsigned int)a1 + 0x18LL));
  LODWORD(result) = *(_QWORD *)((unsigned int)a1 + 0x18LL);
  return result;
}
