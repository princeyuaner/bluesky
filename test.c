#include <stdio.h>

struct test
{
    int data;
};

struct test *a;

void test_func()
{
    struct test b;
    b.data = 1;
    a = &b;
}

int main()
{
    test_func();
    printf("data:%d", a->data);
    return 0;
}
