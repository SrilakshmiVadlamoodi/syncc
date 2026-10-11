/* Mirror of samples/order_loop_mix.mini. Inputs come from argv so the optimizer cannot
 * evaluate the whole program at compile time. */
#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline)) int kernel(int a, int b, int n) {
    /* a is the loop-invariant input k */
    int i = 0, s = 0, c, r;
    (void)b;
    while (i < n) {
        c = a;
        r = c * 2;
        s = s + r;
        i = i + 1;
    }
    return s;
}

int main(int argc, char **argv) {
    if (argc < 4) return 1;
    printf("%d\n", kernel(atoi(argv[1]), atoi(argv[2]), atoi(argv[3])));
    return 0;
}
