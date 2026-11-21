// wasmdec: bounds=hidden
int __usercall switch_dispatch@<r0>(int a1@<r0>, int a2@<r1>)
{
  int v2; // kr08_4
  int result; // eax

  v2 = -1;
  switch ( a1 )
  {
    case 1:
      result = a2 + 10;
      break;
    case 2:
      result = a2 + 20;
      break;
    case 3:
      v2 = a2 + 30;
    default:
      result = v2;
      break;
  }
  return result;
}
