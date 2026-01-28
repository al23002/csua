#include <stdio.h>

int add(int a, int b)
{
    return a + b;
}

int main(int argc, char *argv[])
{
    int result = add(10, 32);
    printf("Hello, %s! The result is %d.\n", "World", result);
    return 0;
}
