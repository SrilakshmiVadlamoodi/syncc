/* Mirror of samples/loop_invariant.mini. Inputs come from argv so the optimizer cannot
 * evaluate the whole program at compile time. */
#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline)) int kernel(int a, int b, int n) {
    int i = 0, result = 0, t;
    while (i < n) {
        t = a * b;
        result = result + t;
        i = i + 1;
    }
    return result;
}

int main(int argc, char **argv) {
    if (argc < 4) return 1;
    printf("%d\n", kernel(atoi(argv[1]), atoi(argv[2]), atoi(argv[3])));
    return 0;
}
