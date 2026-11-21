// wasmdec: bounds=hidden
int __usercall dispatch@<r0>(int a1@<r0>)
{
  int v1; // kr04_4

  v1 = 0;
  switch ( a1 )
  {
    case 0:
      v1 = 10;
      break;
    case 1:
      v1 = 20;
      break;
    case 2:
      v1 = 30;
      break;
    default:
      return v1;
  }
  return v1;
}
