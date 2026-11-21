// wasmdec: bounds=hidden
int __usercall br_table@<r0>(int a1@<r0>)
{
  int result; // eax

  switch ( a1 )
  {
    case 0:
      result = 10;
      break;
    case 1:
      result = 20;
      break;
    default:
      result = 30;
      break;
  }
  return result;
}
