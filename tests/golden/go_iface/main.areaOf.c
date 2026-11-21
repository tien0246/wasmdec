// wasmdec: bounds=hidden
void __usercall main.areaOf(int a1@<r0>)
{
  __int64 v1; // [rsp+0h] [rbp-20h] BYREF
  __int64 v2; // [rsp+30h] [rbp+10h]
  __int64 v3; // [rsp+38h] [rbp+18h]

  if ( v2 == 0 )
    ((void (__usercall *)())runtime.sigpanic)();
  (*(void (__usercall **)(__int64@<r0:r1>))&byte_2[(unsigned int)v2 + 22])(v3);
}
