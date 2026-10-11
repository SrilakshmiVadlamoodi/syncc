#ifndef STRMAP_H
#define STRMAP_H

#include <stddef.h>

/* String -> dense index (0,1,2,... in insertion order). Keys are copied. */
typedef struct StrMap StrMap;

StrMap *strmap_create(void);
void strmap_free(StrMap *m);
int strmap_find(const StrMap *m, const char *key);    /* -1 if absent */
int strmap_intern(StrMap *m, const char *key);        /* index; inserts if new */
size_t strmap_count(const StrMap *m);
const char *strmap_key(const StrMap *m, int idx);

#endif
