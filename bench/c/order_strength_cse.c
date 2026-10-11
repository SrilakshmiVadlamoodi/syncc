/* Mirror of samples/order_strength_cse.mini. Inputs come from argv so the optimizer cannot
 * evaluate the whole program at compile time. */
#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline)) int kernel(int a, int b, int n) {
    int i = 0, s = 0, u, v;
    (void)b;
    while (i < n) {
        a = a + i;
        u = a + a;
        v = a * 2;
        s = s + u + v;
        i = i + 1;
    }
    return s;
}

int main(int argc, char **argv) {
    if (argc < 4) return 1;
    printf("%d\n", kernel(atoi(argv[1]), atoi(argv[2]), atoi(argv[3])));
    return 0;
}
