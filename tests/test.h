#ifndef TEST_H
#define TEST_H

#include <stdio.h>

extern int g_checks;
extern int g_failures;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define TEST_END() do { \
    printf("  %d checks, %d failures\n", g_checks, g_failures); \
    return g_failures ? 1 : 0; \
} while (0)

#endif
