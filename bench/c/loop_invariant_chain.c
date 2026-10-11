/* Mirror of samples/loop_invariant_chain.mini. Inputs come from argv so the optimizer cannot
 * evaluate the whole program at compile time. */
#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline)) int kernel(int a, int b, int n) {
    int c = 4, i = 0, sum = 0, p, q;
    while (i < n) {
        p = a * b;
        q = p + c;
        sum = sum + q;
        i = i + 1;
    }
    return sum;
}

int main(int argc, char **argv) {
    if (argc < 4) return 1;
    printf("%d\n", kernel(atoi(argv[1]), atoi(argv[2]), atoi(argv[3])));
    return 0;
}
