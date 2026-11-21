int foo(void) {
    return 77;
}

int (*fp)(void) = foo;

int call(void) {
    return fp();
}
