/* Mirror of samples/loop_no_invariant.mini. Inputs come from argv so the optimizer cannot
 * evaluate the whole program at compile time. */
#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline)) int kernel(int a, int b, int n) {
    int i = 0, sum = 0;
    (void)a;
    (void)b;
    while (i < n) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}

int main(int argc, char **argv) {
    if (argc < 4) return 1;
    printf("%d\n", kernel(atoi(argv[1]), atoi(argv[2]), atoi(argv[3])));
    return 0;
}
