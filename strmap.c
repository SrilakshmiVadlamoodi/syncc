#include "strmap.h"

#include <stdlib.h>
#include <string.h>

struct StrMap {
    char **keys;        /* dense, insertion order */
    size_t count;
    size_t keys_cap;
    int *table;         /* open addressing; -1 = empty, else index into keys */
    size_t table_cap;   /* power of two */
};

static unsigned long hash_str(const char *s) {
    unsigned long h = 1469598103u;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

StrMap *strmap_create(void) {
    StrMap *m = malloc(sizeof(StrMap));
    m->count = 0;
    m->keys_cap = 32;
    m->keys = malloc(sizeof(char *) * m->keys_cap);
    m->table_cap = 64;
    m->table = malloc(sizeof(int) * m->table_cap);
    for (size_t i = 0; i < m->table_cap; i++) m->table[i] = -1;
    return m;
}

void strmap_free(StrMap *m) {
    if (!m) return;
    for (size_t i = 0; i < m->count; i++) free(m->keys[i]);
    free(m->keys);
    free(m->table);
    free(m);
}

int strmap_find(const StrMap *m, const char *key) {
    size_t i = hash_str(key) & (m->table_cap - 1);
    while (m->table[i] != -1) {
        if (strcmp(m->keys[m->table[i]], key) == 0) return m->table[i];
        i = (i + 1) & (m->table_cap - 1);
    }
    return -1;
}

static void rehash(StrMap *m) {
    size_t new_cap = m->table_cap * 2;
    int *table = malloc(sizeof(int) * new_cap);
    for (size_t i = 0; i < new_cap; i++) table[i] = -1;
    for (size_t k = 0; k < m->count; k++) {
        size_t i = hash_str(m->keys[k]) & (new_cap - 1);
        while (table[i] != -1) i = (i + 1) & (new_cap - 1);
        table[i] = (int)k;
    }
    free(m->table);
    m->table = table;
    m->table_cap = new_cap;
}

int strmap_intern(StrMap *m, const char *key) {
    int found = strmap_find(m, key);
    if (found >= 0) return found;
    if ((m->count + 1) * 2 > m->table_cap) rehash(m);
    if (m->count == m->keys_cap) {
        m->keys_cap *= 2;
        m->keys = realloc(m->keys, sizeof(char *) * m->keys_cap);
    }
    size_t len = strlen(key);
    m->keys[m->count] = malloc(len + 1);
    memcpy(m->keys[m->count], key, len + 1);
    size_t i = hash_str(key) & (m->table_cap - 1);
    while (m->table[i] != -1) i = (i + 1) & (m->table_cap - 1);
    m->table[i] = (int)m->count;
    return (int)m->count++;
}

size_t strmap_count(const StrMap *m) { return m->count; }

const char *strmap_key(const StrMap *m, int idx) { return m->keys[idx]; }
