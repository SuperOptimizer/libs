/* Various token types for lexer fuzz corpus */
#include "header.h"

typedef unsigned long size_t;

struct point {
    int x;
    int y;
};

enum color { RED = 0, GREEN, BLUE = 0xff };

static const char *greeting = "hello\tworld\n";
volatile int counter = 0;

int add(int a, int b)
{
    return a + b;
}

int main(void)
{
    int i;
    int sum;
    int arr[10];
    struct point p;
    char c;
    unsigned short us;
    long l;
    int hex;
    int oct;

    sum = 0;
    hex = 0xDEAD;
    oct = 0777;
    c = 'A';
    us = (unsigned short)42;
    l = 100000L;

    p.x = 10;
    p.y = 20;

    for (i = 0; i < 10; i++) {
        arr[i] = i * i;
        sum += arr[i];
    }

    if (sum > 100) {
        sum = sum % 100;
    } else if (sum == 0) {
        sum = 1;
    }

    while (sum > 0) {
        sum--;
    }

    do {
        counter++;
    } while (counter < 5);

    switch (c) {
    case 'A':
        sum = 1;
        break;
    case 'B':
        sum = 2;
        break;
    default:
        sum = 0;
        break;
    }

    /* operators */
    sum = (hex & 0xff) | (oct ^ 0x55);
    sum = hex << 2;
    sum = hex >> 4;
    sum = ~sum;
    sum = !sum;
    sum = (sum != 0) ? sum : -1;
    sum += 1;
    sum -= 1;
    sum *= 2;
    sum /= 2;
    sum %= 3;
    sum &= 0xf;
    sum |= 0x10;
    sum ^= 0x20;
    sum <<= 1;
    sum >>= 1;

    return sum + add(p.x, p.y) + (int)l + (int)us;
}
