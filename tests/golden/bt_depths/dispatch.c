// wasmdec: bounds=hidden
int __usercall dispatch@<r0>(int a1@<r0>)
{
  int v1; // kr04_4

  v1 = 0;
  switch ( a1 )
  {
    case 1:
      v1 = 3;
      break;
    case 2:
      v1 = 2;
      break;
    case 3:
      v1 = 1;
      break;
    case 4:
      return v1;
    default:
      v1 = 4;
      break;
  }
  return v1;
}
