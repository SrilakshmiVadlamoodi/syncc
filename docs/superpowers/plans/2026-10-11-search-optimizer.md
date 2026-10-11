# Self-Verifying, Search-Based Optimizer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a cost model, TAC interpreter, four new optimization passes, a pass-ordering search, a feature-keyed heuristic cache, an attribution trace, a differential fuzzer, compile-time profiling, and an optional GCC side table to the `minicc` mini compiler.

**Architecture:** Every pass keeps the existing `TACList *pass(TACList *)` signature (returns a NEW list, input untouched). A TAC interpreter provides the weighted dynamic-cost metric and the correctness oracle. A breadth-first search over pass sequences, memoized by TAC hash, picks the cheapest sequence; a nearest-neighbour cache over quantized program features reuses searched results; a fuzzer checks every pass and sequence against the interpreter.

**Tech Stack:** C11 (`gcc -std=c11 -Wall -Wextra`), GNU make, bash scripts. No third-party libraries.

**Spec:** `docs/superpowers/specs/2026-10-11-search-optimizer-design.md`

## Build environment

Run commands from the repo root in Git Bash. If `make` is not on `PATH`, use `/c/msys64/usr/bin/make` (or `/c/msys64/ucrt64/bin/mingw32-make.exe`). The commands below write `make`. Executables are `./minicc` (Windows adds `.exe` automatically).

## Deviations from the spec found while planning (read before starting)

Reading the existing code showed three soundness problems the spec did not know about. The interpreter oracle exists precisely to catch these, so they are fixed in their own tasks, with tests that fail first:

1. **Constant folding changes value kind / precision (Task 4).** `fold_binop` prints floats with `%g`, so `2.0 * 2.0` folds to `4` (an *int* literal, changing later division semantics) and `0.1 + 0.2` loses digits.
2. **LICM hoists user-variable assignments (Task 5).** `x = a * b` hoisted before a loop that runs zero times assigns `x` when the original would not, and hoisting a division can introduce a divide-by-zero trap. Fix: LICM hoists **compiler temps only** (`t<N>`), and divisions only by a nonzero literal. Effect on existing samples: `loop_invariant.mini` still hoists the multiply (`t1 = a * b`), but `t = t1` stays in the loop. README/REPORT are updated in Task 15.
3. **Temp-name collision (Task 9).** A user variable literally named `t1` is indistinguishable from a codegen temp by spelling. The driver detects this and skips optimization with a warning.

Spec parameters kept as written: `max_depth = 4`, `budget = 2000`, `step_limit = 1,000,000`, `K = 4` fuzz environments.

## Global Constraints

- C11, compiled with `-std=c11 -Wall -Wextra`; no new warnings.
- Pass signature: `TACList *pass(TACList *input)`; returns a NEW list; input untouched; caller owns the result (spec §3).
- Cost weights (spec §5): assign=1, `+`/`-`=1, comparison=1, `*`=3, `/`=10, ifFalse=2, goto=1, label=0.
- Interpreter step limit default 1,000,000; division by zero stops with `INTERP_DIV_ZERO` (spec §6).
- Equivalence compares **user variables** (symbol-table names), never temps; an original run that traps or hits the step limit is skipped, not failed (spec §6).
- `x*0` is NOT rewritten by strength reduction (spec §7).
- Basic-block scope: a block ends at `TAC_LABEL`, `TAC_GOTO`, `TAC_IF_FALSE_GOTO`; new passes reset their state at each (spec §7).
- Search defaults: `max_depth = 4`, `budget = 2000` (spec §8).
- Cache file name `.minicc_cache`; line format `<7 features>\t<pass,pass,...>\t<ratio>` (spec §9.1).
- `./minicc <file>` with no flags keeps today's staged output (new sections only appear with flags).
- Every `*_free` path is called; no leaks in new code.

## Review Focus

Inputs the spec implies but doesn't spell out. Each has a test in the owning task:

1. **Original program traps or never terminates** (`1 / 0`, `L1: goto L1`): search must return the input unchanged and the fuzzer must skip it, not report a failure. → Task 8 (search), Task 12 (fuzz).
2. **Zero-trip loop assigning a user variable** (`while (0) { x = a * b; }`): LICM must not hoist `x`. → Task 5.
3. **Float kind preserved by folding** (`2.0 * 2.0` must stay a float literal) and no folding that loses precision or overflows. → Task 4.
4. **Empty program / declarations only** (zero TAC instructions): interpreter, passes, search, features and stats must not crash. → Tasks 3, 6, 8, 11.
5. **Corrupt or missing cache file / user variable named like a temp**: cache loads as empty and skips bad lines; temp collision disables optimization with a warning. → Tasks 9, 11.

---

## File Structure

| File | Create/Modify | Responsibility |
|---|---|---|
| `tacutil.c/h` | Create | Shared TAC helpers: dup, copy, literal/temp predicates, instruction/list to string |
| `strmap.c/h` | Create | String → dense-index hash map (interp slots, DCE, LICM, search) |
| `cost.c/h` | Create | Cost table, per-instruction weight, static op count |
| `interp.c/h` | Create | `Env`, `Value`, `interp_run`, `interp_equivalent`, `interp_dynamic_cost` |
| `passes.c/h` | Create | `copyprop`, `cse`, `strength`, `dce`; registry; `apply_sequence`; `apply_fixed_pipeline` |
| `search.c/h` | Create | Pass-ordering search |
| `pipeline.c/h` | Create | Quiet front end (`frontend_run`), file reading, temp-collision check |
| `heuristic.c/h` | Create | Features, cache, nearest-neighbour lookup, leave-one-out |
| `explain.c/h` | Create | Per-pass cost deltas and instruction diffs |
| `fuzz.c/h` | Create | Program generator + differential checker |
| `timing.h` | Create | `now_seconds()` |
| `optimizer.c/h` | Modify | Use `tacutil`; fold fixes; LICM fixes and O(n) rewrite |
| `codegen.c` | Modify | Use `tac_binop_symbol` from `tacutil` |
| `main.c` | Modify | Option parsing, search/stats/explain/cache/fuzz/gen/loo/time |
| `Makefile` | Modify | Wildcard sources, `make test` |
| `tests/` | Create | `test.h`, `support.c/h`, `test_*.c` |
| `samples/order_*.mini` | Create | Ordering-sensitive benchmarks |
| `bench/` | Create | `run_bench.sh`, `compile_time.sh`, `gcc_compare.sh`, `c/*.c` |

---

### Task 1: Build/test harness, shared TAC helpers, string map, quiet front end

**Files:**
- Create: `tacutil.h`, `tacutil.c`, `strmap.h`, `strmap.c`, `pipeline.h`, `pipeline.c`
- Create: `tests/test.h`, `tests/support.h`, `tests/support.c`, `tests/test_tacutil.c`
- Modify: `Makefile`, `.gitignore`, `codegen.c` (use `tac_binop_symbol`), `optimizer.c` (use `tacutil`)

**Interfaces:**
- Produces (`tacutil.h`):
  - `char *tac_dup_str(const char *s)`
  - `TACInstr tac_copy_instr(const TACInstr *in)`
  - `TACList *tac_copy_list(const TACList *in)`
  - `int tac_is_numeric_literal(const char *s)` (`"5"`, `"-3"`, `"2.5"`; NULL/empty → 0)
  - `int tac_is_float_literal(const char *s)` (numeric literal containing `.`)
  - `int tac_is_temp_name(const char *s)` (`t` followed by 1+ digits)
  - `const char *tac_binop_symbol(TokenType op)`
  - `char *tac_instr_to_string(const TACInstr *in)` (heap; e.g. `t1 = a * b`, `L1:`, `goto L1`, `ifFalse t1 goto L2`)
  - `char *tac_list_to_string(const TACList *list)` (heap; each instruction + `\n`)
- Produces (`strmap.h`): `StrMap *strmap_create(void)`, `void strmap_free(StrMap *)`, `int strmap_find(const StrMap *, const char *key)` (dense index or -1), `int strmap_intern(StrMap *, const char *key)` (index; inserts if new; indices are 0,1,2,… in insertion order), `size_t strmap_count(const StrMap *)`, `const char *strmap_key(const StrMap *, int idx)`.
- Produces (`pipeline.h`): `typedef struct { TACList *tac; SymbolTable *symtab; int parse_errors; int semantic_errors; } FrontEnd;`, `FrontEnd *frontend_run(const char *source)`, `void frontend_free(FrontEnd *)`, `char *pipeline_read_file(const char *path)` (NULL if unreadable), `int frontend_has_temp_collision(const SymbolTable *symtab)`.
- Produces (`tests/support.h`): `TACList *tac_from_text(const char *text)`, `SymbolTable *symtab_of(const char *space_separated_names)`, `FILE *capture_open(void)`, `char *capture_close(FILE *f)` (returns heap text, closes and deletes the file), and `extern const char *SAMPLE_FILES[]` (NULL-terminated), plus `g_checks`, `g_failures`, `CHECK(cond)`, `TEST_END()` from `tests/test.h`.

- [ ] **Step 1: Replace the Makefile**

```make
CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -g
DEPFLAGS := -MMD -MP
TARGET  := minicc

LIB_SRCS  := $(filter-out main.c,$(wildcard *.c))
LIB_OBJS  := $(LIB_SRCS:.c=.o)
MAIN_OBJ  := main.o
TEST_SRCS := $(wildcard tests/test_*.c)
TEST_BINS := $(TEST_SRCS:.c=.bin)

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(MAIN_OBJ) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $(MAIN_OBJ) $(LIB_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -I. -c $< -o $@

tests/%.bin: tests/%.c tests/support.c tests/support.h tests/test.h $(LIB_OBJS)
	$(CC) $(CFLAGS) -I. -o $@ $< tests/support.c $(LIB_OBJS)

# Usage: make run FILE=samples/valid.mini
run: $(TARGET)
	./$(TARGET) $(FILE)

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "== $$t"; ./$$t || exit 1; done
	@echo "ALL TESTS PASSED"

clean:
	rm -f $(LIB_OBJS) $(MAIN_OBJ) $(LIB_OBJS:.o=.d) main.d $(TARGET) $(TARGET).exe $(TEST_BINS) .minicc_cache

-include $(LIB_OBJS:.o=.d) main.d
```

Append to `.gitignore`:

```
*.d
*.bin
.minicc_cache
tests_tmp.out
```

- [ ] **Step 2: Write `tacutil.h` and `tacutil.c`**

`tacutil.h`:

```c
#ifndef TACUTIL_H
#define TACUTIL_H

#include "codegen.h"

char *tac_dup_str(const char *s);
TACInstr tac_copy_instr(const TACInstr *in);
TACList *tac_copy_list(const TACList *in);

int tac_is_numeric_literal(const char *s);
int tac_is_float_literal(const char *s);
int tac_is_temp_name(const char *s);

const char *tac_binop_symbol(TokenType op);
char *tac_instr_to_string(const TACInstr *in);   /* heap; caller frees */
char *tac_list_to_string(const TACList *list);   /* heap; one line per instr */

#endif
```

`tacutil.c`:

```c
#include "tacutil.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *tac_dup_str(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    memcpy(out, s, len + 1);
    return out;
}

TACInstr tac_copy_instr(const TACInstr *in) {
    return (TACInstr){
        .op = in->op,
        .result = in->result ? tac_dup_str(in->result) : NULL,
        .arg1 = in->arg1 ? tac_dup_str(in->arg1) : NULL,
        .arg2 = in->arg2 ? tac_dup_str(in->arg2) : NULL,
        .binop = in->binop
    };
}

TACList *tac_copy_list(const TACList *in) {
    TACList *out = taclist_create();
    for (size_t i = 0; i < in->count; i++)
        taclist_push(out, tac_copy_instr(&in->items[i]));
    return out;
}

int tac_is_numeric_literal(const char *s) {
    if (!s || !*s) return 0;
    size_t i = (s[0] == '-') ? 1 : 0;
    if (!s[i]) return 0;
    int has_digit = 0, has_dot = 0;
    for (; s[i]; i++) {
        if (isdigit((unsigned char)s[i])) has_digit = 1;
        else if (s[i] == '.' && !has_dot) has_dot = 1;
        else return 0;
    }
    return has_digit;
}

int tac_is_float_literal(const char *s) {
    return tac_is_numeric_literal(s) && strchr(s, '.') != NULL;
}

int tac_is_temp_name(const char *s) {
    if (!s || s[0] != 't' || !s[1]) return 0;
    for (size_t i = 1; s[i]; i++)
        if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

const char *tac_binop_symbol(TokenType op) {
    switch (op) {
        case TOK_PLUS:  return "+";
        case TOK_MINUS: return "-";
        case TOK_STAR:  return "*";
        case TOK_SLASH: return "/";
        case TOK_EQ:    return "==";
        case TOK_LT:    return "<";
        case TOK_GT:    return ">";
        case TOK_LE:    return "<=";
        case TOK_GE:    return ">=";
        default:        return "?";
    }
}

char *tac_instr_to_string(const TACInstr *in) {
    const char *r = in->result ? in->result : "";
    const char *a = in->arg1 ? in->arg1 : "";
    const char *b = in->arg2 ? in->arg2 : "";
    size_t cap = strlen(r) + strlen(a) + strlen(b) + 32;
    char *out = malloc(cap);
    switch (in->op) {
        case TAC_ASSIGN:
            snprintf(out, cap, "%s = %s", r, a);
            break;
        case TAC_BINOP:
            snprintf(out, cap, "%s = %s %s %s", r, a, tac_binop_symbol(in->binop), b);
            break;
        case TAC_LABEL:
            snprintf(out, cap, "%s:", r);
            break;
        case TAC_GOTO:
            snprintf(out, cap, "goto %s", r);
            break;
        case TAC_IF_FALSE_GOTO:
            snprintf(out, cap, "ifFalse %s goto %s", a, r);
            break;
    }
    return out;
}

char *tac_list_to_string(const TACList *list) {
    size_t cap = 64, len = 0;
    char *out = malloc(cap);
    out[0] = '\0';
    for (size_t i = 0; i < list->count; i++) {
        char *line = tac_instr_to_string(&list->items[i]);
        size_t n = strlen(line);
        while (len + n + 2 > cap) { cap *= 2; out = realloc(out, cap); }
        memcpy(out + len, line, n);
        len += n;
        out[len++] = '\n';
        out[len] = '\0';
        free(line);
    }
    return out;
}
```

- [ ] **Step 3: Write `strmap.h` and `strmap.c`**

`strmap.h`:

```c
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
```

`strmap.c`:

```c
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
```

- [ ] **Step 4: Write `pipeline.h` and `pipeline.c`**

`pipeline.h`:

```c
#ifndef PIPELINE_H
#define PIPELINE_H

#include "codegen.h"
#include "semantic.h"

typedef struct {
    TACList *tac;
    SymbolTable *symtab;
    int parse_errors;
    int semantic_errors;
} FrontEnd;

/* Quiet lex -> parse -> semantic -> codegen. Never returns NULL.
 * Caller frees with frontend_free. TAC is only meaningful when both
 * error counts are zero. */
FrontEnd *frontend_run(const char *source);
void frontend_free(FrontEnd *fe);

/* Heap copy of the file's contents, or NULL if it cannot be opened. */
char *pipeline_read_file(const char *path);

/* 1 if any user variable is spelled like a codegen temp (t<digits>). */
int frontend_has_temp_collision(const SymbolTable *symtab);

#endif
```

`pipeline.c`:

```c
#include "pipeline.h"

#include <stdio.h>
#include <stdlib.h>

#include "lexer.h"
#include "parser.h"
#include "tacutil.h"

FrontEnd *frontend_run(const char *source) {
    FrontEnd *fe = calloc(1, sizeof(FrontEnd));

    TokenList *tokens = lex(source);
    codegen_reset_counters();
    ParseErrorList *parse_errors = parse_error_list_create();
    AstNode *ast = parse_program(tokens, parse_errors);

    SemanticErrorList *semantic_errors = semantic_error_list_create();
    fe->symtab = analyze(ast, semantic_errors);
    fe->tac = generate_tac(ast);
    fe->parse_errors = (int)parse_errors->count;
    fe->semantic_errors = (int)semantic_errors->count;

    semantic_error_list_free(semantic_errors);
    parse_error_list_free(parse_errors);
    ast_free(ast);
    tokenlist_free(tokens);
    return fe;
}

void frontend_free(FrontEnd *fe) {
    if (!fe) return;
    taclist_free(fe->tac);
    symtab_free(fe->symtab);
    free(fe);
}

char *pipeline_read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

int frontend_has_temp_collision(const SymbolTable *symtab) {
    for (size_t i = 0; i < symtab->count; i++)
        if (tac_is_temp_name(symtab->items[i].name)) return 1;
    return 0;
}
```

- [ ] **Step 5: Refactor `codegen.c` and `optimizer.c` onto `tacutil`**

In `codegen.c`: add `#include "tacutil.h"` after `#include "codegen.h"`; delete the `static const char *binop_symbol(TokenType op) { ... }` function (lines 219-232); replace the two uses `binop_symbol(instr->binop)` with `tac_binop_symbol(instr->binop)`.

In `optimizer.c`: add `#include "tacutil.h"`; delete the three static functions `dup_str` (lines 8-14), `is_numeric_literal` (lines 28-47, with its comment), and `copy_instr` (lines 108-116); then run:

```bash
sed -i 's/\bdup_str\b/tac_dup_str/g; s/\bis_numeric_literal\b/tac_is_numeric_literal/g; s/\bcopy_instr(/tac_copy_instr(/g' optimizer.c
```

- [ ] **Step 6: Write the test support files**

`tests/test.h`:

```c
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
```

`tests/support.h`:

```c
#ifndef SUPPORT_H
#define SUPPORT_H

#include <stdio.h>
#include "codegen.h"
#include "semantic.h"
#include "test.h"

/* One instruction per line: "x = y", "x = y + z", "L1:", "goto L1",
 * "ifFalse x goto L1". Aborts on a malformed line. */
TACList *tac_from_text(const char *text);

/* "a b c" -> symbol table with those names as ints. */
SymbolTable *symtab_of(const char *names);

/* Capture helpers for functions that print to a FILE*. */
FILE *capture_open(void);
char *capture_close(FILE *f);   /* heap text; closes and deletes the file */

extern const char *SAMPLE_FILES[];  /* NULL-terminated */

#endif
```

`tests/support.c`:

```c
#include "support.h"

#include <stdlib.h>
#include <string.h>

#include "tacutil.h"

int g_checks = 0;
int g_failures = 0;

const char *SAMPLE_FILES[] = {
    "samples/valid.mini",
    "samples/constant_folding.mini",
    "samples/loop_invariant.mini",
    "samples/loop_invariant_chain.mini",
    "samples/loop_no_invariant.mini",
    "samples/loop_with_if.mini",
    NULL
};

static int split_ws(char *line, char **tok, int max) {
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (!*p) break;
        tok[n++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r') p++;
        if (*p) *p++ = '\0';
    }
    return n;
}

static TokenType op_from_symbol(const char *s) {
    if (!strcmp(s, "+")) return TOK_PLUS;
    if (!strcmp(s, "-")) return TOK_MINUS;
    if (!strcmp(s, "*")) return TOK_STAR;
    if (!strcmp(s, "/")) return TOK_SLASH;
    if (!strcmp(s, "==")) return TOK_EQ;
    if (!strcmp(s, "<")) return TOK_LT;
    if (!strcmp(s, ">")) return TOK_GT;
    if (!strcmp(s, "<=")) return TOK_LE;
    if (!strcmp(s, ">=")) return TOK_GE;
    return TOK_ERROR;
}

TACList *tac_from_text(const char *text) {
    TACList *list = taclist_create();
    char *copy = tac_dup_str(text);
    char *line = copy;
    while (*line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';
        char *tok[8];
        int n = split_ws(line, tok, 8);
        size_t len0 = n > 0 ? strlen(tok[0]) : 0;
        if (n == 1 && tok[0][len0 - 1] == ':') {
            tok[0][len0 - 1] = '\0';
            taclist_push(list, (TACInstr){ .op = TAC_LABEL, .result = tac_dup_str(tok[0]) });
        } else if (n == 2 && !strcmp(tok[0], "goto")) {
            taclist_push(list, (TACInstr){ .op = TAC_GOTO, .result = tac_dup_str(tok[1]) });
        } else if (n == 4 && !strcmp(tok[0], "ifFalse") && !strcmp(tok[2], "goto")) {
            taclist_push(list, (TACInstr){ .op = TAC_IF_FALSE_GOTO,
                .result = tac_dup_str(tok[3]), .arg1 = tac_dup_str(tok[1]) });
        } else if (n == 3 && !strcmp(tok[1], "=")) {
            taclist_push(list, (TACInstr){ .op = TAC_ASSIGN,
                .result = tac_dup_str(tok[0]), .arg1 = tac_dup_str(tok[2]) });
        } else if (n == 5 && !strcmp(tok[1], "=")) {
            taclist_push(list, (TACInstr){ .op = TAC_BINOP,
                .result = tac_dup_str(tok[0]), .arg1 = tac_dup_str(tok[2]),
                .arg2 = tac_dup_str(tok[4]), .binop = op_from_symbol(tok[3]) });
        } else if (n != 0) {
            fprintf(stderr, "tac_from_text: bad line: %s\n", line);
            abort();
        }
        if (!eol) break;
        line = eol + 1;
    }
    free(copy);
    return list;
}

SymbolTable *symtab_of(const char *names) {
    SymbolTable *t = symtab_create();
    char buf[512];
    strncpy(buf, names, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (char *p = strtok(buf, " "); p; p = strtok(NULL, " "))
        symtab_insert(t, p, TYPE_INT, 0);
    return t;
}

#define CAPTURE_PATH "tests_tmp.out"

FILE *capture_open(void) {
    return fopen(CAPTURE_PATH, "w+b");
}

char *capture_close(FILE *f) {
    fflush(f);
    long n = ftell(f);
    rewind(f);
    char *s = malloc((size_t)n + 1);
    size_t got = fread(s, 1, (size_t)n, f);
    s[got] = '\0';
    fclose(f);
    remove(CAPTURE_PATH);
    return s;
}
```

- [ ] **Step 7: Write the failing test `tests/test_tacutil.c`**

```c
#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "optimizer.h"
#include "pipeline.h"
#include "strmap.h"
#include "tacutil.h"

static void test_predicates(void) {
    CHECK(tac_is_numeric_literal("5"));
    CHECK(tac_is_numeric_literal("-3"));
    CHECK(tac_is_numeric_literal("2.5"));
    CHECK(!tac_is_numeric_literal("t1"));
    CHECK(!tac_is_numeric_literal("-"));
    CHECK(!tac_is_numeric_literal(""));
    CHECK(!tac_is_numeric_literal(NULL));
    CHECK(tac_is_float_literal("2.0"));
    CHECK(!tac_is_float_literal("2"));
    CHECK(tac_is_temp_name("t1"));
    CHECK(tac_is_temp_name("t42"));
    CHECK(!tac_is_temp_name("t"));
    CHECK(!tac_is_temp_name("total"));
    CHECK(!tac_is_temp_name("x1"));
}

static void test_strmap(void) {
    StrMap *m = strmap_create();
    CHECK(strmap_find(m, "a") == -1);
    CHECK(strmap_intern(m, "a") == 0);
    CHECK(strmap_intern(m, "b") == 1);
    CHECK(strmap_intern(m, "a") == 0);
    CHECK(strmap_find(m, "b") == 1);
    CHECK(strmap_count(m) == 2);
    CHECK(strcmp(strmap_key(m, 1), "b") == 0);
    for (int i = 0; i < 1000; i++) {   /* force several rehashes */
        char k[16];
        snprintf(k, sizeof k, "k%d", i);
        strmap_intern(m, k);
    }
    CHECK(strmap_count(m) == 1002);
    CHECK(strmap_find(m, "k999") == 1001);
    strmap_free(m);
}

static void test_text_round_trip(void) {
    const char *text = "x = 5\nt1 = x * 3\nL1:\nifFalse t1 goto L2\ngoto L1\nL2:\n";
    TACList *list = tac_from_text(text);
    CHECK(list->count == 6);
    char *back = tac_list_to_string(list);
    CHECK(strcmp(back, text) == 0);
    free(back);
    taclist_free(list);
}

static void test_fold_baseline(void) {
    TACList *in = tac_from_text("t1 = 2 + 3\nx = t1\n");
    TACList *out = optimize_constant_folding(in);
    char *s = tac_list_to_string(out);
    CHECK(strcmp(s, "t1 = 5\nx = t1\n") == 0);
    free(s);
    taclist_free(in);
    taclist_free(out);
}

static void test_frontend(void) {
    FrontEnd *fe = frontend_run("int x; x = 1 + 2;");
    CHECK(fe->parse_errors == 0);
    CHECK(fe->semantic_errors == 0);
    CHECK(fe->tac->count == 2);
    CHECK(!frontend_has_temp_collision(fe->symtab));
    frontend_free(fe);

    FrontEnd *bad = frontend_run("int t1; t1 = 2;");
    CHECK(frontend_has_temp_collision(bad->symtab));
    frontend_free(bad);
}

int main(void) {
    test_predicates();
    test_strmap();
    test_text_round_trip();
    test_fold_baseline();
    test_frontend();
    TEST_END();
}
```

- [ ] **Step 8: Run the test and confirm it fails to build**

Run: `make test`
Expected: before Steps 2-6 exist, the build fails on missing headers; after creating all files above, proceed to Step 9.

- [ ] **Step 9: Run the test and confirm it passes**

Run: `make test && make && ./minicc samples/valid.mini | head -20`
Expected: `ALL TESTS PASSED`; `minicc` still prints `=== Tokens ===` etc. exactly as before.

- [ ] **Step 10: Commit**

```bash
git add Makefile .gitignore tacutil.c tacutil.h strmap.c strmap.h pipeline.c pipeline.h codegen.c optimizer.c tests/
git commit -m "build: test harness, shared TAC helpers, string map, quiet front end

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Cost model

**Files:**
- Create: `cost.h`, `cost.c`, `tests/test_cost.c`

**Interfaces:**
- Consumes: `TACInstr`, `TACList` (`codegen.h`), `TokenType` constants.
- Produces: `int cost_of_instr(const TACInstr *in)`, `long cost_static_ops(const TACList *list)` (count of non-label instructions), `void cost_model_print(FILE *out)`, `extern const CostModel COST_MODEL_DEFAULT`.

- [ ] **Step 1: Write the failing test `tests/test_cost.c`**

```c
#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "cost.h"

static int cost_of(const char *line) {
    TACList *l = tac_from_text(line);
    int c = cost_of_instr(&l->items[0]);
    taclist_free(l);
    return c;
}

int main(void) {
    CHECK(cost_of("x = 5\n") == 1);
    CHECK(cost_of("t = a + b\n") == 1);
    CHECK(cost_of("t = a - b\n") == 1);
    CHECK(cost_of("t = a < b\n") == 1);
    CHECK(cost_of("t = a == b\n") == 1);
    CHECK(cost_of("t = a * b\n") == 3);
    CHECK(cost_of("t = a / b\n") == 10);
    CHECK(cost_of("ifFalse t goto L1\n") == 2);
    CHECK(cost_of("goto L1\n") == 1);
    CHECK(cost_of("L1:\n") == 0);

    TACList *l = tac_from_text("x = 1\nL1:\nt = x * 2\ngoto L1\n");
    CHECK(cost_static_ops(l) == 3);   /* labels are not counted */
    taclist_free(l);

    TACList *empty = tac_from_text("");
    CHECK(cost_static_ops(empty) == 0);
    taclist_free(empty);

    TEST_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `make test`
Expected: FAIL to build (`cost.h: No such file`).

- [ ] **Step 3: Write `cost.h` and `cost.c`**

`cost.h`:

```c
#ifndef COST_H
#define COST_H

#include <stdio.h>
#include "codegen.h"

typedef struct {
    int assign;
    int add_sub;
    int compare;
    int mul;
    int div;
    int if_false_goto;
    int jump;
    int label;
} CostModel;

extern const CostModel COST_MODEL_DEFAULT;

/* Weight of one executed instruction under COST_MODEL_DEFAULT. */
int cost_of_instr(const TACInstr *in);

/* Number of non-label instructions (static size). */
long cost_static_ops(const TACList *list);

/* One line listing every weight, for reports. */
void cost_model_print(FILE *out);

#endif
```

`cost.c`:

```c
#include "cost.h"

const CostModel COST_MODEL_DEFAULT = {
    .assign = 1, .add_sub = 1, .compare = 1, .mul = 3, .div = 10,
    .if_false_goto = 2, .jump = 1, .label = 0
};

int cost_of_instr(const TACInstr *in) {
    const CostModel *m = &COST_MODEL_DEFAULT;
    switch (in->op) {
        case TAC_ASSIGN:        return m->assign;
        case TAC_LABEL:         return m->label;
        case TAC_GOTO:          return m->jump;
        case TAC_IF_FALSE_GOTO: return m->if_false_goto;
        case TAC_BINOP:
            switch (in->binop) {
                case TOK_STAR:  return m->mul;
                case TOK_SLASH: return m->div;
                case TOK_PLUS:
                case TOK_MINUS: return m->add_sub;
                default:        return m->compare;
            }
    }
    return 0;
}

long cost_static_ops(const TACList *list) {
    long n = 0;
    for (size_t i = 0; i < list->count; i++)
        if (list->items[i].op != TAC_LABEL) n++;
    return n;
}

void cost_model_print(FILE *out) {
    const CostModel *m = &COST_MODEL_DEFAULT;
    fprintf(out, "cost weights: assign=%d add/sub=%d compare=%d mul=%d div=%d "
                 "ifFalse=%d goto=%d label=%d\n",
            m->assign, m->add_sub, m->compare, m->mul, m->div,
            m->if_false_goto, m->jump, m->label);
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `make test`
Expected: `ALL TESTS PASSED`

- [ ] **Step 5: Commit**

```bash
git add cost.c cost.h tests/test_cost.c
git commit -m "feat: add TAC cost model

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: TAC interpreter (metric and oracle)

**Files:**
- Create: `interp.h`, `interp.c`, `tests/test_interp.c`

**Interfaces:**
- Consumes: `StrMap` (`strmap.h`), `cost_of_instr` (`cost.h`), `tac_*` helpers, `SymbolTable` (`semantic.h`).
- Produces (`interp.h`):
  - `typedef enum { VAL_INT, VAL_FLOAT } ValKind; typedef struct { ValKind kind; long i; double f; } Value; Value value_int(long); Value value_float(double);`
  - `typedef struct Env Env; Env *env_create(void); void env_set(Env *, const char *name, Value); int env_get(const Env *, const char *name, Value *out);` (1 if present) `size_t env_size(const Env *); const char *env_name_at(const Env *, size_t i); Value env_value_at(const Env *, size_t i); Env *env_copy(const Env *); void env_free(Env *);`
  - `typedef enum { INTERP_OK, INTERP_STEP_LIMIT, INTERP_DIV_ZERO } InterpStatus;`
  - `typedef struct { InterpStatus status; long dynamic_cost; long steps; Env *final_env; } InterpResult;`
  - `InterpResult interp_run(const TACList *list, const Env *initial, long step_limit);` (`initial` may be NULL)
  - `void interp_result_free(InterpResult *r);`
  - `int interp_equivalent(const InterpResult *a, const InterpResult *b, const SymbolTable *user_vars);`
  - `long interp_dynamic_cost(const TACList *list, const Env *initial);` (-1 unless status is OK; uses `INTERP_DEFAULT_STEP_LIMIT` = 1000000)
  - `#define INTERP_DEFAULT_STEP_LIMIT 1000000L`

- [ ] **Step 1: Write the failing test `tests/test_interp.c`**

```c
#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "interp.h"

static InterpResult run(const char *text) {
    TACList *l = tac_from_text(text);
    InterpResult r = interp_run(l, NULL, INTERP_DEFAULT_STEP_LIMIT);
    taclist_free(l);
    return r;
}

static void test_straight_line(void) {
    InterpResult r = run("x = 5\ny = x * 3\n");
    CHECK(r.status == INTERP_OK);
    Value v;
    CHECK(env_get(r.final_env, "y", &v) && v.kind == VAL_INT && v.i == 15);
    CHECK(r.dynamic_cost == 4);   /* assign 1 + mul 3 */
    CHECK(r.steps == 2);
    interp_result_free(&r);
}

static void test_loop_cost_and_steps(void) {
    InterpResult r = run("x = 3\nL1:\nt1 = x > 0\nifFalse t1 goto L2\n"
                         "t2 = x - 1\nx = t2\ngoto L1\nL2:\n");
    CHECK(r.status == INTERP_OK);
    Value v;
    CHECK(env_get(r.final_env, "x", &v) && v.i == 0);
    CHECK(r.dynamic_cost == 22);
    CHECK(r.steps == 23);
    interp_result_free(&r);
}

static void test_float_and_int_division(void) {
    InterpResult r = run("a = 1.5\nb = a + 2\nc = 7\nd = c / 2\n");
    Value v;
    CHECK(env_get(r.final_env, "b", &v) && v.kind == VAL_FLOAT && v.f == 3.5);
    CHECK(env_get(r.final_env, "d", &v) && v.kind == VAL_INT && v.i == 3);
    interp_result_free(&r);
}

static void test_traps_and_limits(void) {
    InterpResult r = run("a = 0\nb = 1 / a\n");
    CHECK(r.status == INTERP_DIV_ZERO);
    interp_result_free(&r);

    TACList *l = tac_from_text("L1:\ngoto L1\n");
    InterpResult s = interp_run(l, NULL, 100);
    CHECK(s.status == INTERP_STEP_LIMIT);
    CHECK(s.steps == 100);
    interp_result_free(&s);
    CHECK(interp_dynamic_cost(l, NULL) == -1);
    taclist_free(l);
}

static void test_initial_env_and_passthrough(void) {
    Env *init = env_create();
    env_set(init, "x", value_int(41));
    env_set(init, "unused", value_int(9));
    TACList *l = tac_from_text("y = x + 1\n");
    InterpResult r = interp_run(l, init, INTERP_DEFAULT_STEP_LIMIT);
    Value v;
    CHECK(env_get(r.final_env, "y", &v) && v.i == 42);
    CHECK(env_get(r.final_env, "x", &v) && v.i == 41);
    CHECK(env_get(r.final_env, "unused", &v) && v.i == 9);   /* untouched names survive */
    interp_result_free(&r);
    taclist_free(l);
    env_free(init);
}

static void test_equivalence(void) {
    SymbolTable *sym = symtab_of("x y");
    InterpResult a = run("t1 = 2 + 3\nx = t1\ny = 1\n");
    InterpResult b = run("x = 5\ny = 1\n");
    InterpResult c = run("x = 5\ny = 2\n");
    InterpResult d = run("x = 5.0\ny = 1\n");   /* same number, different kind */
    CHECK(interp_equivalent(&a, &b, sym));       /* temps ignored */
    CHECK(!interp_equivalent(&a, &c, sym));
    CHECK(!interp_equivalent(&a, &d, sym));
    interp_result_free(&a); interp_result_free(&b);
    interp_result_free(&c); interp_result_free(&d);
    symtab_free(sym);
}

static void test_empty_program(void) {
    TACList *l = tac_from_text("");
    InterpResult r = interp_run(l, NULL, INTERP_DEFAULT_STEP_LIMIT);
    CHECK(r.status == INTERP_OK);
    CHECK(r.dynamic_cost == 0 && r.steps == 0);
    interp_result_free(&r);
    CHECK(interp_dynamic_cost(l, NULL) == 0);
    taclist_free(l);
}

int main(void) {
    test_straight_line();
    test_loop_cost_and_steps();
    test_float_and_int_division();
    test_traps_and_limits();
    test_initial_env_and_passthrough();
    test_equivalence();
    test_empty_program();
    TEST_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `make test`
Expected: FAIL to build (`interp.h: No such file`).

- [ ] **Step 3: Write `interp.h`**

```c
#ifndef INTERP_H
#define INTERP_H

#include <stddef.h>
#include <stdio.h>
#include "codegen.h"
#include "semantic.h"

#define INTERP_DEFAULT_STEP_LIMIT 1000000L

typedef enum { VAL_INT, VAL_FLOAT } ValKind;
typedef struct { ValKind kind; long i; double f; } Value;

Value value_int(long i);
Value value_float(double f);

/* name -> Value map (opaque). */
typedef struct Env Env;
Env *env_create(void);
void env_set(Env *e, const char *name, Value v);
int env_get(const Env *e, const char *name, Value *out);   /* 1 if present */
size_t env_size(const Env *e);
const char *env_name_at(const Env *e, size_t i);
Value env_value_at(const Env *e, size_t i);
Env *env_copy(const Env *e);
void env_free(Env *e);

typedef enum { INTERP_OK, INTERP_STEP_LIMIT, INTERP_DIV_ZERO } InterpStatus;

typedef struct {
    InterpStatus status;
    long dynamic_cost;   /* sum of cost over executed instructions */
    long steps;          /* instructions executed */
    Env *final_env;      /* owned by the result */
} InterpResult;

/* Executes `list`. Names never written read as `initial`'s value, or
 * integer 0 if absent. `initial` may be NULL. Names in `initial` appear in
 * final_env even if the program never touches them. */
InterpResult interp_run(const TACList *list, const Env *initial, long step_limit);
void interp_result_free(InterpResult *r);

/* 1 iff both runs finished INTERP_OK and every symbol-table variable has
 * the same kind and value in both final environments (a name absent from
 * an environment reads as integer 0; two NaNs compare equal). */
int interp_equivalent(const InterpResult *a, const InterpResult *b,
                      const SymbolTable *user_vars);

/* Dynamic cost of a run with the default step limit, or -1 unless the
 * status is INTERP_OK. */
long interp_dynamic_cost(const TACList *list, const Env *initial);

#endif
```

- [ ] **Step 4: Write `interp.c`**

```c
#include "interp.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "cost.h"
#include "strmap.h"
#include "tacutil.h"

/* ---------- values and environments ---------- */

Value value_int(long i) { return (Value){ .kind = VAL_INT, .i = i, .f = 0.0 }; }
Value value_float(double f) { return (Value){ .kind = VAL_FLOAT, .i = 0, .f = f }; }

struct Env {
    StrMap *map;
    Value *vals;
    size_t cap;
};

Env *env_create(void) {
    Env *e = malloc(sizeof(Env));
    e->map = strmap_create();
    e->cap = 16;
    e->vals = malloc(sizeof(Value) * e->cap);
    return e;
}

void env_set(Env *e, const char *name, Value v) {
    int idx = strmap_intern(e->map, name);
    while ((size_t)idx >= e->cap) {
        e->cap *= 2;
        e->vals = realloc(e->vals, sizeof(Value) * e->cap);
    }
    e->vals[idx] = v;
}

int env_get(const Env *e, const char *name, Value *out) {
    int idx = strmap_find(e->map, name);
    if (idx < 0) return 0;
    if (out) *out = e->vals[idx];
    return 1;
}

size_t env_size(const Env *e) { return strmap_count(e->map); }
const char *env_name_at(const Env *e, size_t i) { return strmap_key(e->map, (int)i); }
Value env_value_at(const Env *e, size_t i) { return e->vals[i]; }

Env *env_copy(const Env *e) {
    Env *c = env_create();
    for (size_t i = 0; i < env_size(e); i++)
        env_set(c, env_name_at(e, i), env_value_at(e, i));
    return c;
}

void env_free(Env *e) {
    if (!e) return;
    strmap_free(e->map);
    free(e->vals);
    free(e);
}

/* ---------- arithmetic (kept consistent with fold_binop) ---------- */

static int is_comparison(TokenType op) {
    return op == TOK_EQ || op == TOK_LT || op == TOK_GT || op == TOK_LE || op == TOK_GE;
}

static double as_double(Value v) { return v.kind == VAL_FLOAT ? v.f : (double)v.i; }

static Value eval_binop(TokenType op, Value a, Value b, int *div_zero) {
    *div_zero = 0;
    int af = a.kind == VAL_FLOAT, bf = b.kind == VAL_FLOAT;

    if (is_comparison(op)) {
        int r = 0;
        if (!af && !bf) {
            long x = a.i, y = b.i;
            switch (op) {
                case TOK_EQ: r = x == y; break;
                case TOK_LT: r = x < y; break;
                case TOK_GT: r = x > y; break;
                case TOK_LE: r = x <= y; break;
                default:     r = x >= y; break;
            }
        } else {
            double x = as_double(a), y = as_double(b);
            switch (op) {
                case TOK_EQ: r = x == y; break;
                case TOK_LT: r = x < y; break;
                case TOK_GT: r = x > y; break;
                case TOK_LE: r = x <= y; break;
                default:     r = x >= y; break;
            }
        }
        return value_int(r);
    }

    if (af || bf) {
        double x = as_double(a), y = as_double(b), r = 0.0;
        switch (op) {
            case TOK_PLUS:  r = x + y; break;
            case TOK_MINUS: r = x - y; break;
            case TOK_STAR:  r = x * y; break;
            case TOK_SLASH:
                if (y == 0.0) { *div_zero = 1; return value_float(0.0); }
                r = x / y;
                break;
            default: break;
        }
        return value_float(r);
    }

    unsigned long ux = (unsigned long)a.i, uy = (unsigned long)b.i;
    switch (op) {
        case TOK_PLUS:  return value_int((long)(ux + uy));
        case TOK_MINUS: return value_int((long)(ux - uy));
        case TOK_STAR:  return value_int((long)(ux * uy));
        case TOK_SLASH:
            if (b.i == 0) { *div_zero = 1; return value_int(0); }
            if (a.i == LONG_MIN && b.i == -1) return value_int(LONG_MIN);
            return value_int(a.i / b.i);
        default: return value_int(0);
    }
}

static int is_zero(Value v) {
    return v.kind == VAL_FLOAT ? v.f == 0.0 : v.i == 0;
}

/* ---------- execution ---------- */

typedef struct { int is_lit; Value lit; int slot; } Opnd;

static Opnd make_opnd(const char *s, StrMap *slots) {
    Opnd o = { 0, value_int(0), 0 };
    if (!s) { o.is_lit = 1; return o; }
    if (tac_is_numeric_literal(s)) {
        o.is_lit = 1;
        o.lit = tac_is_float_literal(s) ? value_float(strtod(s, NULL))
                                        : value_int(strtol(s, NULL, 10));
    } else {
        o.slot = strmap_intern(slots, s);
    }
    return o;
}

static Value read_opnd(const Opnd *o, const Value *vals) {
    return o->is_lit ? o->lit : vals[o->slot];
}

InterpResult interp_run(const TACList *list, const Env *initial, long step_limit) {
    size_t n = list->count;
    StrMap *slots = strmap_create();
    StrMap *labels = strmap_create();

    Opnd *a1 = malloc(sizeof(Opnd) * (n + 1));
    Opnd *a2 = malloc(sizeof(Opnd) * (n + 1));
    int *res = malloc(sizeof(int) * (n + 1));
    size_t *target = malloc(sizeof(size_t) * (n + 1));
    size_t *label_pos = malloc(sizeof(size_t) * (n + 1));

    for (size_t i = 0; i < n; i++) {
        const TACInstr *in = &list->items[i];
        a1[i] = make_opnd(in->op == TAC_BINOP || in->op == TAC_ASSIGN || in->op == TAC_IF_FALSE_GOTO
                              ? in->arg1 : NULL, slots);
        a2[i] = make_opnd(in->op == TAC_BINOP ? in->arg2 : NULL, slots);
        res[i] = (in->op == TAC_ASSIGN || in->op == TAC_BINOP) ? strmap_intern(slots, in->result) : -1;
        if (in->op == TAC_LABEL) {
            int before = (int)strmap_count(labels);
            int id = strmap_intern(labels, in->result);
            if (id == before) label_pos[id] = i;
        }
    }
    for (size_t i = 0; i < n; i++) {
        const TACInstr *in = &list->items[i];
        target[i] = n;
        if (in->op == TAC_GOTO || in->op == TAC_IF_FALSE_GOTO) {
            int id = strmap_find(labels, in->result);
            if (id >= 0) target[i] = label_pos[id];
        }
    }

    size_t nslots = strmap_count(slots);
    Value *vals = malloc(sizeof(Value) * (nslots + 1));
    for (size_t s = 0; s < nslots; s++) vals[s] = value_int(0);
    if (initial) {
        for (size_t k = 0; k < env_size(initial); k++) {
            int s = strmap_find(slots, env_name_at(initial, k));
            if (s >= 0) vals[s] = env_value_at(initial, k);
        }
    }

    InterpResult out = { INTERP_OK, 0, 0, NULL };
    size_t pc = 0;
    while (pc < n) {
        if (out.steps >= step_limit) { out.status = INTERP_STEP_LIMIT; break; }
        out.steps++;
        const TACInstr *in = &list->items[pc];
        out.dynamic_cost += cost_of_instr(in);
        switch (in->op) {
            case TAC_ASSIGN:
                vals[res[pc]] = read_opnd(&a1[pc], vals);
                pc++;
                break;
            case TAC_BINOP: {
                int dz = 0;
                Value v = eval_binop(in->binop, read_opnd(&a1[pc], vals), read_opnd(&a2[pc], vals), &dz);
                if (dz) { out.status = INTERP_DIV_ZERO; pc = n; break; }
                vals[res[pc]] = v;
                pc++;
                break;
            }
            case TAC_LABEL:
                pc++;
                break;
            case TAC_GOTO:
                pc = target[pc];
                break;
            case TAC_IF_FALSE_GOTO:
                pc = is_zero(read_opnd(&a1[pc], vals)) ? target[pc] : pc + 1;
                break;
        }
    }

    out.final_env = initial ? env_copy(initial) : env_create();
    for (size_t s = 0; s < nslots; s++)
        env_set(out.final_env, strmap_key(slots, (int)s), vals[s]);

    free(vals);
    free(label_pos);
    free(target);
    free(res);
    free(a2);
    free(a1);
    strmap_free(labels);
    strmap_free(slots);
    return out;
}

void interp_result_free(InterpResult *r) {
    env_free(r->final_env);
    r->final_env = NULL;
}

int interp_equivalent(const InterpResult *a, const InterpResult *b,
                      const SymbolTable *user_vars) {
    if (a->status != INTERP_OK || b->status != INTERP_OK) return 0;
    for (size_t i = 0; i < user_vars->count; i++) {
        const char *name = user_vars->items[i].name;
        Value va = value_int(0), vb = value_int(0);
        env_get(a->final_env, name, &va);
        env_get(b->final_env, name, &vb);
        if (va.kind != vb.kind) return 0;
        if (va.kind == VAL_INT) {
            if (va.i != vb.i) return 0;
        } else if (va.f != vb.f) {
            if (!(va.f != va.f && vb.f != vb.f)) return 0;   /* both NaN is equal */
        }
    }
    return 1;
}

long interp_dynamic_cost(const TACList *list, const Env *initial) {
    InterpResult r = interp_run(list, initial, INTERP_DEFAULT_STEP_LIMIT);
    long cost = r.status == INTERP_OK ? r.dynamic_cost : -1;
    interp_result_free(&r);
    return cost;
}
```

- [ ] **Step 5: Run to verify it passes**

Run: `make test`
Expected: `ALL TESTS PASSED`

- [ ] **Step 6: Commit**

```bash
git add interp.c interp.h tests/test_interp.c
git commit -m "feat: add TAC interpreter (cost metric and correctness oracle)

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Fix constant-folding kind, precision and overflow bugs

**Files:**
- Modify: `optimizer.c` (replace `fold_binop`; delete `float_to_str`)
- Create: `tests/test_fold.c`

**Interfaces:**
- Consumes: `optimize_constant_folding`, `interp_run`, `interp_equivalent`.
- Produces: no signature changes. Behaviour: float results always print with a `.`; results that are non-finite, would print in exponent form, or integer arithmetic that overflows are NOT folded (instruction left unchanged); integer comparisons use `long`, not `double`.

- [ ] **Step 1: Write the failing test `tests/test_fold.c`**

```c
#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "interp.h"
#include "optimizer.h"
#include "tacutil.h"

static char *fold_text(const char *in_text) {
    TACList *in = tac_from_text(in_text);
    TACList *out = optimize_constant_folding(in);
    char *s = tac_list_to_string(out);
    taclist_free(in);
    taclist_free(out);
    return s;
}

static void expect(const char *in, const char *want) {
    char *got = fold_text(in);
    if (strcmp(got, want) != 0) printf("  folded %s-> %s(wanted %s)\n", in, got, want);
    CHECK(strcmp(got, want) == 0);
    free(got);
}

int main(void) {
    expect("t1 = 2 + 3\n", "t1 = 5\n");
    expect("t1 = 3 < 4\n", "t1 = 1\n");
    expect("t1 = 7 / 2\n", "t1 = 3\n");

    /* float results must stay float literals (contain '.') */
    expect("t1 = 2.0 * 2.0\n", "t1 = 4.0\n");
    expect("t1 = 1.5 + 2\n", "t1 = 3.5\n");

    /* not folded: divide by zero, overflow, results that need exponent form */
    expect("t1 = 1.0 / 0.0\n", "t1 = 1.0 / 0.0\n");
    expect("t1 = 5 / 0\n", "t1 = 5 / 0\n");
    expect("t1 = 9223372036854775807 + 1\n", "t1 = 9223372036854775807 + 1\n");
    expect("t1 = 99999999999999999999999999999999999999999.0 * 99999999999999999999999999999999999999999.0\n",
           "t1 = 99999999999999999999999999999999999999999.0 * 99999999999999999999999999999999999999999.0\n");

    /* precision: folded value must equal the runtime value exactly */
    {
        TACList *in = tac_from_text("t1 = 0.1 + 0.2\nx = t1\n");
        TACList *out = optimize_constant_folding(in);
        SymbolTable *sym = symtab_of("x");
        InterpResult a = interp_run(in, NULL, INTERP_DEFAULT_STEP_LIMIT);
        InterpResult b = interp_run(out, NULL, INTERP_DEFAULT_STEP_LIMIT);
        CHECK(interp_equivalent(&a, &b, sym));
        interp_result_free(&a);
        interp_result_free(&b);
        symtab_free(sym);
        taclist_free(in);
        taclist_free(out);
    }

    TEST_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `make test`
Expected: FAIL: `t1 = 2.0 * 2.0` folds to `t1 = 4`, and `0.1 + 0.2` loses precision, and the overflow case is folded.

- [ ] **Step 3: Replace `fold_binop` and delete `float_to_str`**

In `optimizer.c`, add `#include <limits.h>` and `#include <math.h>` to the includes, delete the `float_to_str` function, and replace the whole `fold_binop` function (and its comment) with:

```c
/* Formats `v` as a float literal that parses back to exactly `v`, always
 * containing a '.' so it stays a float literal. Fails (returns 0) for
 * non-finite values and for values that would print in exponent form,
 * because tac_is_numeric_literal cannot represent those. */
static int format_float_literal(double v, char **out) {
    char buf[64];
    if (!isfinite(v)) return 0;
    for (int prec = 6; prec <= 17; prec++) {
        snprintf(buf, sizeof(buf), "%.*g", prec, v);
        if (strtod(buf, NULL) == v) break;
    }
    if (strpbrk(buf, "eE") != NULL) return 0;
    if (strchr(buf, '.') == NULL) strcat(buf, ".0");
    *out = tac_dup_str(buf);
    return 1;
}

/* Attempts to fold `a op b` into a single literal. Returns 1 and sets
 * *out (heap-allocated) on success; returns 0 (leaving *out untouched)
 * when the operator isn't foldable, would divide by zero, would overflow
 * integer arithmetic, or the float result cannot be written back as a
 * plain literal -- callers keep the original TAC_BINOP in that case. */
static int fold_binop(TokenType op, const char *a, const char *b, char **out) {
    int a_float = tac_is_float_literal(a);
    int b_float = tac_is_float_literal(b);

    if (is_comparison_op(op)) {
        int r;
        if (!a_float && !b_float) {
            long x = strtol(a, NULL, 10), y = strtol(b, NULL, 10);
            switch (op) {
                case TOK_EQ: r = (x == y); break;
                case TOK_LT: r = (x < y); break;
                case TOK_GT: r = (x > y); break;
                case TOK_LE: r = (x <= y); break;
                case TOK_GE: r = (x >= y); break;
                default: return 0;
            }
        } else {
            double x = atof(a), y = atof(b);
            switch (op) {
                case TOK_EQ: r = (x == y); break;
                case TOK_LT: r = (x < y); break;
                case TOK_GT: r = (x > y); break;
                case TOK_LE: r = (x <= y); break;
                case TOK_GE: r = (x >= y); break;
                default: return 0;
            }
        }
        *out = int_to_str(r);
        return 1;
    }

    if (a_float || b_float) {
        double x = atof(a), y = atof(b), r;
        switch (op) {
            case TOK_PLUS:  r = x + y; break;
            case TOK_MINUS: r = x - y; break;
            case TOK_STAR:  r = x * y; break;
            case TOK_SLASH:
                if (y == 0.0) return 0;
                r = x / y;
                break;
            default: return 0;
        }
        return format_float_literal(r, out);
    }

    long x = strtol(a, NULL, 10), y = strtol(b, NULL, 10), r;
    switch (op) {
        case TOK_PLUS:  if (__builtin_add_overflow(x, y, &r)) return 0; break;
        case TOK_MINUS: if (__builtin_sub_overflow(x, y, &r)) return 0; break;
        case TOK_STAR:  if (__builtin_mul_overflow(x, y, &r)) return 0; break;
        case TOK_SLASH:
            if (y == 0 || (x == LONG_MIN && y == -1)) return 0;
            r = x / y;
            break;
        default: return 0;
    }
    *out = int_to_str(r);
    return 1;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `make test && ./minicc samples/constant_folding.mini | sed -n '/optimized: constant folding/,/loop-invariant/p'`
Expected: `ALL TESTS PASSED`; the folded output for the sample is unchanged from before (`a = 14`-style integer folds).

- [ ] **Step 5: Commit**

```bash
git add optimizer.c tests/test_fold.c
git commit -m "fix: constant folding preserves float kind, precision, and skips overflow

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Fix unsound loop-invariant code motion

**Files:**
- Modify: `optimizer.c` (inside `optimize_loop_invariant_code_motion`, pass 1)
- Create: `tests/test_licm.c`

**Interfaces:**
- Consumes: `optimize_loop_invariant_code_motion`, `tac_is_temp_name`, `interp_*`.
- Produces: LICM hoists only instructions whose result is a compiler temp, and `/` only when the divisor is a nonzero numeric literal. Signature unchanged.

- [ ] **Step 1: Write the failing test `tests/test_licm.c`**

```c
#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "interp.h"
#include "optimizer.h"
#include "tacutil.h"

static char *licm_text(const char *in_text) {
    TACList *in = tac_from_text(in_text);
    TACList *out = optimize_loop_invariant_code_motion(in);
    char *s = tac_list_to_string(out);
    taclist_free(in);
    taclist_free(out);
    return s;
}

static void expect(const char *in, const char *want) {
    char *got = licm_text(in);
    if (strcmp(got, want) != 0) printf("  got:\n%s  wanted:\n%s", got, want);
    CHECK(strcmp(got, want) == 0);
    free(got);
}

#define LOOP_HEAD "i = 0\nL1:\nt1 = i < 3\nifFalse t1 goto L2\n"
#define LOOP_TAIL "t9 = i + 1\ni = t9\ngoto L1\nL2:\n"

int main(void) {
    /* a user variable assigned in the body is never hoisted (zero-trip safety) */
    expect(LOOP_HEAD "x = a * b\n" LOOP_TAIL,
           LOOP_HEAD "x = a * b\n" LOOP_TAIL);

    /* an invariant temp is hoisted before the loop label */
    expect(LOOP_HEAD "t3 = a * b\nx = t3\n" LOOP_TAIL,
           "i = 0\nt3 = a * b\nL1:\nt1 = i < 3\nifFalse t1 goto L2\nx = t3\n" LOOP_TAIL);

    /* division by a variable is not hoisted (could trap on a zero-trip loop) */
    expect(LOOP_HEAD "t3 = a / b\nx = t3\n" LOOP_TAIL,
           LOOP_HEAD "t3 = a / b\nx = t3\n" LOOP_TAIL);

    /* division by a nonzero literal is hoisted */
    expect(LOOP_HEAD "t3 = a / 2\nx = t3\n" LOOP_TAIL,
           "i = 0\nt3 = a / 2\nL1:\nt1 = i < 3\nifFalse t1 goto L2\nx = t3\n" LOOP_TAIL);

    /* division by literal zero is not hoisted */
    expect(LOOP_HEAD "t3 = a / 0\nx = t3\n" LOOP_TAIL,
           LOOP_HEAD "t3 = a / 0\nx = t3\n" LOOP_TAIL);

    /* zero-trip loop with a hoistable user-variable assignment: x must stay 0 */
    {
        TACList *in = tac_from_text("i = 5\nL1:\nt1 = i < 3\nifFalse t1 goto L2\n"
                                    "x = a * b\nt9 = i + 1\ni = t9\ngoto L1\nL2:\n");
        TACList *out = optimize_loop_invariant_code_motion(in);
        Env *init = env_create();
        env_set(init, "a", value_int(2));
        env_set(init, "b", value_int(3));
        SymbolTable *sym = symtab_of("i x a b");
        InterpResult r0 = interp_run(in, init, INTERP_DEFAULT_STEP_LIMIT);
        InterpResult r1 = interp_run(out, init, INTERP_DEFAULT_STEP_LIMIT);
        CHECK(interp_equivalent(&r0, &r1, sym));
        Value v;
        CHECK(env_get(r1.final_env, "x", &v) && v.i == 0);
        interp_result_free(&r0);
        interp_result_free(&r1);
        symtab_free(sym);
        env_free(init);
        taclist_free(in);
        taclist_free(out);
    }

    TEST_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `make test`
Expected: FAIL: `x = a * b` is hoisted, and `a / b` is hoisted.

- [ ] **Step 3: Add the two guards**

In `optimizer.c`, inside the `for (size_t k = body_start; ...)` loop of pass 1, immediately after the line `if (bi->op != TAC_ASSIGN && bi->op != TAC_BINOP) continue;`, insert:

```c
            /* Only compiler temps are hoisted: a user variable may be read
             * before its assignment in the body, or be observed after a loop
             * that never runs. */
            if (!tac_is_temp_name(bi->result)) continue;
            /* A hoisted division could trap on a loop that never executes. */
            if (bi->op == TAC_BINOP && bi->binop == TOK_SLASH &&
                !(tac_is_numeric_literal(bi->arg2) && atof(bi->arg2) != 0.0)) continue;
```

Also update the header comment in `optimizer.h` for `optimize_loop_invariant_code_motion`: replace "hoists any TAC_ASSIGN/TAC_BINOP instruction whose operands are all loop-invariant and whose result is assigned exactly once in the body" with "hoists any TAC_ASSIGN/TAC_BINOP instruction whose result is a compiler temp (t<N>), whose operands are all loop-invariant, whose result is assigned exactly once in the body, and which is not a division by a non-literal or zero divisor".

- [ ] **Step 4: Run to verify it passes**

Run: `make test && make && ./minicc samples/loop_invariant.mini | sed -n '/loop-invariant code motion/,$p'`
Expected: `ALL TESTS PASSED`; the sample's multiply (`t? = a * b`) now sits before the loop label while `t = t?` remains inside the loop.

- [ ] **Step 5: Commit**

```bash
git add optimizer.c optimizer.h tests/test_licm.c
git commit -m "fix: LICM hoists only compiler temps and never non-literal divisions

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Pass registry, copy propagation, strength reduction

**Files:**
- Create: `passes.h`, `passes.c`, `tests/test_passes.c`

**Interfaces:**
- Consumes: `optimize_constant_folding`, `optimize_loop_invariant_code_motion`, `tac_*` helpers.
- Produces (`passes.h`):
  - `typedef TACList *(*PassFn)(TACList *input); typedef struct { const char *name; PassFn fn; } PassInfo;`
  - `extern const PassInfo PASS_REGISTRY[];` (terminated by `{NULL, NULL}`), `size_t pass_count(void)`, `int pass_index(const char *name)` (-1 if unknown)
  - `TACList *pass_copy_propagation(TACList *)`, `TACList *pass_strength_reduction(TACList *)` (Task 7 adds `pass_cse`, `pass_dce` to the same header and registry)
  - `TACList *apply_sequence(TACList *input, const int *seq, int len)` (new list; empty sequence returns a copy)
  - `TACList *apply_fixed_pipeline(TACList *input)` (fold then licm; new list)
  - Registry order after this task: `fold`, `licm`, `copyprop`, `strength`.

- [ ] **Step 1: Write the failing test `tests/test_passes.c`**

```c
#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "interp.h"
#include "passes.h"
#include "pipeline.h"
#include "tacutil.h"

static void expect_pass(PassFn fn, const char *in_text, const char *want) {
    TACList *in = tac_from_text(in_text);
    TACList *out = fn(in);
    char *got = tac_list_to_string(out);
    if (strcmp(got, want) != 0) printf("  pass output:\n%s  wanted:\n%s", got, want);
    CHECK(strcmp(got, want) == 0);
    free(got);
    taclist_free(in);
    taclist_free(out);
}

static void test_registry(void) {
    CHECK(pass_index("fold") == 0);
    CHECK(pass_index("licm") == 1);
    CHECK(pass_index("copyprop") == 2);
    CHECK(pass_index("strength") == 3);
    CHECK(pass_index("nope") == -1);
    CHECK(PASS_REGISTRY[pass_count()].name == NULL);

    TACList *in = tac_from_text("x = 2 + 3\n");
    int seq[1] = { 0 };
    TACList *out = apply_sequence(in, seq, 1);
    char *s = tac_list_to_string(out);
    CHECK(strcmp(s, "x = 5\n") == 0);
    free(s);
    taclist_free(out);
    TACList *same = apply_sequence(in, NULL, 0);
    CHECK(same->count == 1 && same != in);
    taclist_free(same);
    taclist_free(in);
}

static void test_copyprop(void) {
    expect_pass(pass_copy_propagation,
                "x = 4\ny = x\nt1 = y * 2\n",
                "x = 4\ny = 4\nt1 = 4 * 2\n");
    /* source reassigned: the copy is no longer valid */
    expect_pass(pass_copy_propagation, "x = a\na = 5\ny = x\n", "x = a\na = 5\ny = x\n");
    /* state resets at labels */
    expect_pass(pass_copy_propagation, "x = 4\nL1:\ny = x\n", "x = 4\nL1:\ny = x\n");
    /* branch condition operand is rewritten */
    expect_pass(pass_copy_propagation, "c = t1\nifFalse c goto L1\nL1:\n",
                "c = t1\nifFalse t1 goto L1\nL1:\n");
    /* self-referential update does not create a bogus copy */
    expect_pass(pass_copy_propagation, "x = y\nx = x + 1\nz = x\n",
                "x = y\nx = y + 1\nz = x\n");
    /* empty program */
    expect_pass(pass_copy_propagation, "", "");
}

static void test_strength(void) {
    expect_pass(pass_strength_reduction, "t1 = x * 2\n", "t1 = x + x\n");
    expect_pass(pass_strength_reduction, "t1 = 2 * x\n", "t1 = x + x\n");
    expect_pass(pass_strength_reduction, "t1 = x * 1\n", "t1 = x\n");
    expect_pass(pass_strength_reduction, "t1 = 1 * x\n", "t1 = x\n");
    expect_pass(pass_strength_reduction, "t1 = x + 0\n", "t1 = x\n");
    expect_pass(pass_strength_reduction, "t1 = 0 + x\n", "t1 = x\n");
    expect_pass(pass_strength_reduction, "t1 = x - 0\n", "t1 = x\n");
    expect_pass(pass_strength_reduction, "t1 = x / 1\n", "t1 = x\n");
    /* never rewritten: x*0 (float inf/NaN), float literals, 0 - x */
    expect_pass(pass_strength_reduction, "t1 = x * 0\n", "t1 = x * 0\n");
    expect_pass(pass_strength_reduction, "t1 = x * 2.0\n", "t1 = x * 2.0\n");
    expect_pass(pass_strength_reduction, "t1 = 0 - x\n", "t1 = 0 - x\n");
}

/* Every registered pass must preserve semantics on every sample and never
 * increase dynamic cost on them. */
static void check_pass_on_source(const char *path) {
    char *src = pipeline_read_file(path);
    CHECK(src != NULL);
    if (!src) return;
    FrontEnd *fe = frontend_run(src);
    CHECK(fe->parse_errors == 0 && fe->semantic_errors == 0);
    InterpResult ref = interp_run(fe->tac, NULL, INTERP_DEFAULT_STEP_LIMIT);
    CHECK(ref.status == INTERP_OK);
    for (size_t p = 0; p < pass_count(); p++) {
        TACList *out = PASS_REGISTRY[p].fn(fe->tac);
        InterpResult r = interp_run(out, NULL, INTERP_DEFAULT_STEP_LIMIT);
        int eq = interp_equivalent(&ref, &r, fe->symtab);
        if (!eq) printf("  pass %s broke %s\n", PASS_REGISTRY[p].name, path);
        CHECK(eq);
        CHECK(r.dynamic_cost <= ref.dynamic_cost);
        interp_result_free(&r);
        taclist_free(out);
    }
    interp_result_free(&ref);
    frontend_free(fe);
    free(src);
}

static void test_property_on_samples(void) {
    for (int i = 0; SAMPLE_FILES[i]; i++) check_pass_on_source(SAMPLE_FILES[i]);
}

int main(void) {
    test_registry();
    test_copyprop();
    test_strength();
    test_property_on_samples();
    TEST_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `make test`
Expected: FAIL to build (`passes.h: No such file`).

- [ ] **Step 3: Write `passes.h`**

```c
#ifndef PASSES_H
#define PASSES_H

#include <stddef.h>
#include "codegen.h"

/* Every pass returns a NEW list and leaves `input` untouched. */
typedef TACList *(*PassFn)(TACList *input);

typedef struct {
    const char *name;
    PassFn fn;
} PassInfo;

/* NULL-name terminated; order is stable (indices are used in sequences). */
extern const PassInfo PASS_REGISTRY[];
size_t pass_count(void);
int pass_index(const char *name);   /* -1 if unknown */

/* Block-scoped passes (a block ends at a label / goto / ifFalse). */
TACList *pass_copy_propagation(TACList *input);
TACList *pass_strength_reduction(TACList *input);

/* Applies PASS_REGISTRY[seq[0]], then seq[1], ... Returns a new list; an
 * empty sequence returns a copy of `input`. */
TACList *apply_sequence(TACList *input, const int *seq, int len);

/* The original fixed pipeline: constant folding, then LICM. */
TACList *apply_fixed_pipeline(TACList *input);

#endif
```

- [ ] **Step 4: Write `passes.c`**

```c
#include "passes.h"

#include <stdlib.h>
#include <string.h>

#include "optimizer.h"
#include "tacutil.h"

static int is_control(TACOp op) {
    return op == TAC_LABEL || op == TAC_GOTO || op == TAC_IF_FALSE_GOTO;
}

/* ================= copy propagation ================= */

typedef struct { const char *key; const char *val; } CopyEntry;
typedef struct { CopyEntry *items; size_t count, cap; } CopyMap;

static const char *copy_lookup(const CopyMap *m, const char *operand) {
    if (!operand || tac_is_numeric_literal(operand)) return operand;
    for (size_t i = 0; i < m->count; i++)
        if (strcmp(m->items[i].key, operand) == 0) return m->items[i].val;
    return operand;
}

static void copy_kill(CopyMap *m, const char *name) {
    size_t w = 0;
    for (size_t r = 0; r < m->count; r++) {
        if (strcmp(m->items[r].key, name) == 0 || strcmp(m->items[r].val, name) == 0) continue;
        m->items[w++] = m->items[r];
    }
    m->count = w;
}

static void copy_add(CopyMap *m, const char *key, const char *val) {
    if (m->count == m->cap) {
        m->cap = m->cap ? m->cap * 2 : 16;
        m->items = realloc(m->items, sizeof(CopyEntry) * m->cap);
    }
    m->items[m->count++] = (CopyEntry){ key, val };
}

TACList *pass_copy_propagation(TACList *input) {
    TACList *out = taclist_create();
    CopyMap map = { NULL, 0, 0 };

    for (size_t i = 0; i < input->count; i++) {
        const TACInstr *in = &input->items[i];
        TACInstr c = tac_copy_instr(in);

        if (in->op == TAC_LABEL) {
            map.count = 0;
        } else {
            if (in->op == TAC_ASSIGN || in->op == TAC_BINOP || in->op == TAC_IF_FALSE_GOTO) {
                const char *r1 = copy_lookup(&map, in->arg1);
                if (r1 != in->arg1) { free(c.arg1); c.arg1 = tac_dup_str(r1); }
            }
            if (in->op == TAC_BINOP) {
                const char *r2 = copy_lookup(&map, in->arg2);
                if (r2 != in->arg2) { free(c.arg2); c.arg2 = tac_dup_str(r2); }
            }
            if (in->op == TAC_ASSIGN || in->op == TAC_BINOP) {
                copy_kill(&map, in->result);
                /* `x = y` records x -> y; the source string lives in `input`
                 * or is a literal/name copied from it, both valid for the
                 * whole pass, so resolve it against the input operand. */
                if (in->op == TAC_ASSIGN) {
                    const char *src = copy_lookup(&map, in->arg1);
                    if (strcmp(src, in->result) != 0) copy_add(&map, in->result, src);
                }
            }
            if (is_control(in->op)) map.count = 0;
        }
        taclist_push(out, c);
    }
    free(map.items);
    return out;
}

/* ================= strength reduction ================= */

static int is_lit(const char *s, const char *want) {
    return s != NULL && strcmp(s, want) == 0;
}

static void to_assign(TACInstr *c, char *source_taken_from_arg) {
    /* c->arg1 / c->arg2 ownership handled by the caller */
    (void)source_taken_from_arg;
    c->op = TAC_ASSIGN;
    c->binop = 0;
}

TACList *pass_strength_reduction(TACList *input) {
    TACList *out = taclist_create();
    for (size_t i = 0; i < input->count; i++) {
        const TACInstr *in = &input->items[i];
        TACInstr c = tac_copy_instr(in);

        if (in->op == TAC_BINOP) {
            const char *a = in->arg1, *b = in->arg2;
            TokenType op = in->binop;

            if (op == TOK_STAR && is_lit(b, "2")) {            /* a*2 -> a+a */
                free(c.arg2);
                c.arg2 = tac_dup_str(a);
                c.binop = TOK_PLUS;
            } else if (op == TOK_STAR && is_lit(a, "2")) {     /* 2*b -> b+b */
                free(c.arg1);
                c.arg1 = tac_dup_str(b);
                c.binop = TOK_PLUS;
            } else if ((op == TOK_STAR && is_lit(b, "1")) ||
                       (op == TOK_PLUS && is_lit(b, "0")) ||
                       (op == TOK_MINUS && is_lit(b, "0")) ||
                       (op == TOK_SLASH && is_lit(b, "1"))) {  /* result = a */
                free(c.arg2);
                c.arg2 = NULL;
                to_assign(&c, c.arg1);
            } else if ((op == TOK_STAR && is_lit(a, "1")) ||
                       (op == TOK_PLUS && is_lit(a, "0"))) {   /* result = b */
                free(c.arg1);
                c.arg1 = c.arg2;
                c.arg2 = NULL;
                to_assign(&c, c.arg1);
            }
        }
        taclist_push(out, c);
    }
    return out;
}

/* ================= registry and helpers ================= */

const PassInfo PASS_REGISTRY[] = {
    { "fold",     optimize_constant_folding },
    { "licm",     optimize_loop_invariant_code_motion },
    { "copyprop", pass_copy_propagation },
    { "strength", pass_strength_reduction },
    { NULL, NULL }
};

size_t pass_count(void) {
    size_t n = 0;
    while (PASS_REGISTRY[n].name) n++;
    return n;
}

int pass_index(const char *name) {
    for (size_t i = 0; PASS_REGISTRY[i].name; i++)
        if (strcmp(PASS_REGISTRY[i].name, name) == 0) return (int)i;
    return -1;
}

TACList *apply_sequence(TACList *input, const int *seq, int len) {
    TACList *cur = tac_copy_list(input);
    for (int i = 0; i < len; i++) {
        TACList *next = PASS_REGISTRY[seq[i]].fn(cur);
        taclist_free(cur);
        cur = next;
    }
    return cur;
}

TACList *apply_fixed_pipeline(TACList *input) {
    TACList *folded = optimize_constant_folding(input);
    TACList *hoisted = optimize_loop_invariant_code_motion(folded);
    taclist_free(folded);
    return hoisted;
}
```

Note the test expectation `x = y\nx = x + 1\nz = x` → `x = y\nx = y + 1\nz = x`: after `x = y` the map holds x→y; the next instruction's `x` operand is rewritten to `y`, then the write to `x` kills the x→y entry, and `z = x` is left alone. This is correct because `x` was reassigned.

- [ ] **Step 5: Run to verify it passes**

Run: `make test`
Expected: `ALL TESTS PASSED`

- [ ] **Step 6: Commit**

```bash
git add passes.c passes.h tests/test_passes.c
git commit -m "feat: pass registry, copy propagation, strength reduction

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Common subexpression elimination and dead code elimination

**Files:**
- Modify: `passes.h`, `passes.c`, `tests/test_passes.c`

**Interfaces:**
- Consumes: Task 6 registry and helpers; `StrMap`.
- Produces: `TACList *pass_cse(TACList *)`, `TACList *pass_dce(TACList *)`; registry order becomes `fold`, `licm`, `copyprop`, `strength`, `cse`, `dce` (indices 0-5).

- [ ] **Step 1: Add the failing tests**

In `tests/test_passes.c`, add before `main`:

```c
static void test_cse(void) {
    expect_pass(pass_cse, "t1 = a * b\nt2 = a * b\nx = t1 + t2\n",
                "t1 = a * b\nt2 = t1\nx = t1 + t2\n");
    /* commutative operators match with swapped operands */
    expect_pass(pass_cse, "t1 = a + b\nt2 = b + a\n", "t1 = a + b\nt2 = t1\n");
    /* non-commutative operators do not */
    expect_pass(pass_cse, "t1 = a - b\nt2 = b - a\n", "t1 = a - b\nt2 = b - a\n");
    /* an operand reassigned in between invalidates the expression */
    expect_pass(pass_cse, "t1 = a * b\na = 1\nt2 = a * b\n", "t1 = a * b\na = 1\nt2 = a * b\n");
    /* the holder reassigned in between invalidates it too */
    expect_pass(pass_cse, "t1 = a * b\nt1 = 7\nt2 = a * b\n", "t1 = a * b\nt1 = 7\nt2 = a * b\n");
    /* state resets at labels */
    expect_pass(pass_cse, "t1 = a * b\nL1:\nt2 = a * b\n", "t1 = a * b\nL1:\nt2 = a * b\n");
    /* an instruction overwriting its own operand is not recorded */
    expect_pass(pass_cse, "a = a + b\nt2 = a + b\n", "a = a + b\nt2 = a + b\n");
}

static void test_dce(void) {
    expect_pass(pass_dce, "t1 = a * b\nx = 5\n", "x = 5\n");
    /* chains are removed to a fixed point */
    expect_pass(pass_dce, "t1 = a + 1\nt2 = t1 * 2\nx = 5\n", "x = 5\n");
    /* a temp that is read stays */
    expect_pass(pass_dce, "t1 = a * b\nx = t1\n", "t1 = a * b\nx = t1\n");
    /* overwritten store in the same block is removed */
    expect_pass(pass_dce, "x = 1\nx = 2\n", "x = 2\n");
    /* ... unless it is read in between */
    expect_pass(pass_dce, "x = 1\ny = x\nx = 2\n", "x = 1\ny = x\nx = 2\n");
    /* ... or a label separates them */
    expect_pass(pass_dce, "x = 1\nL1:\nx = 2\n", "x = 1\nL1:\nx = 2\n");
    /* x = x + 1 reads x before writing it */
    expect_pass(pass_dce, "x = 1\nx = x + 1\n", "x = 1\nx = x + 1\n");
    /* user variables' final values are observable */
    expect_pass(pass_dce, "x = 5\n", "x = 5\n");
    expect_pass(pass_dce, "", "");
}
```

Update `test_registry` to also check `pass_index("cse") == 4` and `pass_index("dce") == 5`, and add `test_cse(); test_dce();` to `main`.

- [ ] **Step 2: Run to verify it fails**

Run: `make test`
Expected: FAIL to build (`pass_cse` undeclared).

- [ ] **Step 3: Declare the passes**

In `passes.h`, after `pass_strength_reduction`, add:

```c
TACList *pass_cse(TACList *input);
TACList *pass_dce(TACList *input);
```

- [ ] **Step 4: Implement them**

In `passes.c`, add `#include "strmap.h"` and insert before the "registry and helpers" section:

```c
/* ================= common subexpression elimination ================= */

typedef struct { TokenType op; const char *a, *b, *holder; } Avail;
typedef struct { Avail *items; size_t count, cap; } AvailSet;

static const char *avail_find(const AvailSet *s, TokenType op, const char *a, const char *b) {
    int commutative = (op == TOK_PLUS || op == TOK_STAR);
    for (size_t i = 0; i < s->count; i++) {
        if (s->items[i].op != op) continue;
        if (!strcmp(s->items[i].a, a) && !strcmp(s->items[i].b, b)) return s->items[i].holder;
        if (commutative && !strcmp(s->items[i].a, b) && !strcmp(s->items[i].b, a))
            return s->items[i].holder;
    }
    return NULL;
}

static void avail_kill(AvailSet *s, const char *name) {
    size_t w = 0;
    for (size_t r = 0; r < s->count; r++) {
        const Avail *e = &s->items[r];
        if (!strcmp(e->holder, name) || !strcmp(e->a, name) || !strcmp(e->b, name)) continue;
        s->items[w++] = *e;
    }
    s->count = w;
}

static void avail_add(AvailSet *s, TokenType op, const char *a, const char *b, const char *holder) {
    if (s->count == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->items = realloc(s->items, sizeof(Avail) * s->cap);
    }
    s->items[s->count++] = (Avail){ op, a, b, holder };
}

TACList *pass_cse(TACList *input) {
    TACList *out = taclist_create();
    AvailSet set = { NULL, 0, 0 };

    for (size_t i = 0; i < input->count; i++) {
        const TACInstr *in = &input->items[i];
        TACInstr c = tac_copy_instr(in);

        if (in->op == TAC_LABEL) {
            set.count = 0;
        } else if (in->op == TAC_BINOP) {
            const char *holder = avail_find(&set, in->binop, in->arg1, in->arg2);
            int replaced = 0;
            if (holder) {
                free(c.arg1);
                free(c.arg2);
                c.op = TAC_ASSIGN;
                c.arg1 = tac_dup_str(holder);
                c.arg2 = NULL;
                c.binop = 0;
                replaced = 1;
            }
            avail_kill(&set, in->result);
            if (!replaced && strcmp(in->result, in->arg1) != 0 && strcmp(in->result, in->arg2) != 0)
                avail_add(&set, in->binop, in->arg1, in->arg2, in->result);
        } else if (in->op == TAC_ASSIGN) {
            avail_kill(&set, in->result);
        } else if (is_control(in->op)) {
            set.count = 0;
        }
        taclist_push(out, c);
    }
    free(set.items);
    return out;
}

/* ================= dead code elimination ================= */

#define NO_PENDING ((size_t)-1)

static int reads_arg1(TACOp op) {
    return op == TAC_ASSIGN || op == TAC_BINOP || op == TAC_IF_FALSE_GOTO;
}

TACList *pass_dce(TACList *input) {
    size_t n = input->count;
    char *dead = calloc(n + 1, 1);

    /* (a) temps that are never read, to a fixed point */
    int changed = 1;
    while (changed) {
        changed = 0;
        StrMap *reads = strmap_create();
        for (size_t i = 0; i < n; i++) {
            if (dead[i]) continue;
            const TACInstr *in = &input->items[i];
            if (reads_arg1(in->op) && in->arg1 && !tac_is_numeric_literal(in->arg1))
                strmap_intern(reads, in->arg1);
            if (in->op == TAC_BINOP && in->arg2 && !tac_is_numeric_literal(in->arg2))
                strmap_intern(reads, in->arg2);
        }
        for (size_t i = 0; i < n; i++) {
            if (dead[i]) continue;
            const TACInstr *in = &input->items[i];
            if ((in->op == TAC_ASSIGN || in->op == TAC_BINOP) &&
                tac_is_temp_name(in->result) && strmap_find(reads, in->result) < 0) {
                dead[i] = 1;
                changed = 1;
            }
        }
        strmap_free(reads);
    }

    /* (b) a store overwritten later in the same block with no read between */
    size_t *pending = malloc(sizeof(size_t) * (n + 1));
    StrMap *names = strmap_create();
    for (size_t i = 0; i < n; i++) {
        if (dead[i]) continue;
        const TACInstr *in = &input->items[i];

        if (in->op == TAC_LABEL) {
            strmap_free(names);
            names = strmap_create();
            continue;
        }
        if (reads_arg1(in->op) && in->arg1 && !tac_is_numeric_literal(in->arg1)) {
            int id = strmap_find(names, in->arg1);
            if (id >= 0) pending[id] = NO_PENDING;
        }
        if (in->op == TAC_BINOP && in->arg2 && !tac_is_numeric_literal(in->arg2)) {
            int id = strmap_find(names, in->arg2);
            if (id >= 0) pending[id] = NO_PENDING;
        }
        if (in->op == TAC_ASSIGN || in->op == TAC_BINOP) {
            int before = (int)strmap_count(names);
            int id = strmap_intern(names, in->result);
            if (id == before) pending[id] = NO_PENDING;
            if (pending[id] != NO_PENDING) dead[pending[id]] = 1;
            pending[id] = i;
        }
        if (is_control(in->op)) {
            strmap_free(names);
            names = strmap_create();
        }
    }
    strmap_free(names);
    free(pending);

    TACList *out = taclist_create();
    for (size_t i = 0; i < n; i++)
        if (!dead[i]) taclist_push(out, tac_copy_instr(&input->items[i]));
    free(dead);
    return out;
}
```

Update the registry to:

```c
const PassInfo PASS_REGISTRY[] = {
    { "fold",     optimize_constant_folding },
    { "licm",     optimize_loop_invariant_code_motion },
    { "copyprop", pass_copy_propagation },
    { "strength", pass_strength_reduction },
    { "cse",      pass_cse },
    { "dce",      pass_dce },
    { NULL, NULL }
};
```

- [ ] **Step 5: Run to verify it passes**

Run: `make test`
Expected: `ALL TESTS PASSED` (the property test now covers `cse` and `dce` on every sample).

- [ ] **Step 6: Commit**

```bash
git add passes.c passes.h tests/test_passes.c
git commit -m "feat: add CSE and dead code elimination passes

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Pass-ordering search

**Files:**
- Create: `timing.h`, `search.h`, `search.c`, `tests/test_search.c`

**Interfaces:**
- Consumes: `PASS_REGISTRY`, `pass_count`, `apply_fixed_pipeline`, `interp_run`, `interp_dynamic_cost`, `tac_list_to_string`, `tac_copy_list`.
- Produces:
  - `timing.h`: `static inline double now_seconds(void)`.
  - `search.h`: `#define SEARCH_MAX_SEQ 16`; `typedef struct { int max_depth; int budget; } SearchConfig;`; `SearchConfig search_default_config(void)` (depth 4, budget 2000); `typedef struct { TACList *best; int sequence[SEARCH_MAX_SEQ]; int sequence_len; long best_cost; long baseline_cost; long input_cost; int candidates; int invalid_candidates; double seconds; } SearchResult;`; `SearchResult search_best_sequence(TACList *input, const SearchConfig *cfg, const Env *initial)`; `void search_result_free(SearchResult *r)`; `void search_print_sequence(const SearchResult *r, FILE *out)` (prints `fold, copyprop, ...` or `(none)`).
  - Behaviour: if the input does not run to `INTERP_OK` under `initial`, the result is a copy of the input with `sequence_len = 0`, `candidates = 0`.

- [ ] **Step 1: Write the failing test `tests/test_search.c`**

```c
#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "interp.h"
#include "passes.h"
#include "search.h"
#include "tacutil.h"

static void test_beats_fixed_pipeline(void) {
    TACList *in = tac_from_text("x = 4\ny = x\nt1 = y * 2\nz = t1\n");
    SearchConfig cfg = search_default_config();
    SearchResult r = search_best_sequence(in, &cfg, NULL);

    CHECK(r.input_cost == 6);
    CHECK(r.baseline_cost == 6);          /* fold + licm find nothing */
    CHECK(r.best_cost <= 3);              /* copyprop, fold, copyprop, dce */
    CHECK(r.best_cost < r.baseline_cost);
    CHECK(r.sequence_len >= 1);
    CHECK(r.candidates > 0 && r.candidates <= cfg.budget);

    SymbolTable *sym = symtab_of("x y z");
    InterpResult a = interp_run(in, NULL, INTERP_DEFAULT_STEP_LIMIT);
    InterpResult b = interp_run(r.best, NULL, INTERP_DEFAULT_STEP_LIMIT);
    CHECK(interp_equivalent(&a, &b, sym));
    interp_result_free(&a);
    interp_result_free(&b);
    symtab_free(sym);

    /* replaying the reported sequence reproduces `best` exactly */
    TACList *again = apply_sequence(in, r.sequence, r.sequence_len);
    char *s1 = tac_list_to_string(again);
    char *s2 = tac_list_to_string(r.best);
    CHECK(strcmp(s1, s2) == 0);
    free(s1);
    free(s2);
    taclist_free(again);

    search_result_free(&r);
    taclist_free(in);
}

static void test_never_worse_than_baseline(void) {
    const char *programs[] = {
        "i = 0\ns = 0\nL1:\nt1 = i < 5\nifFalse t1 goto L2\nt2 = s + i\ns = t2\nt3 = i + 1\ni = t3\ngoto L1\nL2:\n",
        "a = 2 + 3\nb = a * 1\n",
        "x = y\n",
        NULL
    };
    for (int i = 0; programs[i]; i++) {
        TACList *in = tac_from_text(programs[i]);
        SearchConfig cfg = search_default_config();
        SearchResult r = search_best_sequence(in, &cfg, NULL);
        CHECK(r.best_cost <= r.baseline_cost);
        CHECK(r.best_cost <= r.input_cost);
        search_result_free(&r);
        taclist_free(in);
    }
}

static void test_budget_is_respected(void) {
    TACList *in = tac_from_text("x = 4\ny = x\nt1 = y * 2\nz = t1\n");
    SearchConfig cfg = { 4, 3 };
    SearchResult r = search_best_sequence(in, &cfg, NULL);
    CHECK(r.candidates <= 3);
    search_result_free(&r);
    taclist_free(in);
}

static void test_unscoreable_input_returned_unchanged(void) {
    TACList *loop = tac_from_text("L1:\ngoto L1\n");
    SearchConfig cfg = search_default_config();
    SearchResult r = search_best_sequence(loop, &cfg, NULL);
    CHECK(r.sequence_len == 0);
    CHECK(r.candidates == 0);
    char *s = tac_list_to_string(r.best);
    CHECK(strcmp(s, "L1:\ngoto L1\n") == 0);
    free(s);
    search_result_free(&r);
    taclist_free(loop);

    TACList *trap = tac_from_text("a = 0\nb = 1 / a\n");
    SearchResult t = search_best_sequence(trap, &cfg, NULL);
    CHECK(t.sequence_len == 0 && t.candidates == 0);
    search_result_free(&t);
    taclist_free(trap);
}

static void test_empty_program(void) {
    TACList *in = tac_from_text("");
    SearchConfig cfg = search_default_config();
    SearchResult r = search_best_sequence(in, &cfg, NULL);
    CHECK(r.best != NULL && r.best->count == 0);
    CHECK(r.best_cost == 0);
    search_result_free(&r);
    taclist_free(in);
}

int main(void) {
    test_beats_fixed_pipeline();
    test_never_worse_than_baseline();
    test_budget_is_respected();
    test_unscoreable_input_returned_unchanged();
    test_empty_program();
    TEST_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `make test`
Expected: FAIL to build (`search.h: No such file`).

- [ ] **Step 3: Write `timing.h`**

```c
#ifndef TIMING_H
#define TIMING_H

#include <time.h>

/* Wall-clock seconds as a double (C11 timespec_get). */
static inline double now_seconds(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

#endif
```

- [ ] **Step 4: Write `search.h`**

```c
#ifndef SEARCH_H
#define SEARCH_H

#include <stdio.h>
#include "codegen.h"
#include "interp.h"

#define SEARCH_MAX_SEQ 16

typedef struct {
    int max_depth;   /* sequence length bound */
    int budget;      /* max candidate evaluations */
} SearchConfig;

typedef struct {
    TACList *best;                    /* owned */
    int sequence[SEARCH_MAX_SEQ];     /* indices into PASS_REGISTRY */
    int sequence_len;
    long best_cost;                   /* dynamic cost of `best` */
    long baseline_cost;               /* dynamic cost of the fixed fold -> licm pipeline */
    long input_cost;                  /* dynamic cost of the unoptimized input */
    int candidates;                   /* distinct states evaluated */
    int invalid_candidates;           /* states that failed to run to completion (a pass bug) */
    double seconds;
} SearchResult;

SearchConfig search_default_config(void);   /* depth 4, budget 2000 */

/* Breadth-first over pass sequences from `input`, memoized by TAC hash,
 * scored by the interpreter under `initial` (may be NULL). If the input
 * itself does not run to completion the result is a copy of the input with
 * an empty sequence. */
SearchResult search_best_sequence(TACList *input, const SearchConfig *cfg, const Env *initial);
void search_result_free(SearchResult *r);

/* "fold, copyprop, dce" or "(none)". */
void search_print_sequence(const SearchResult *r, FILE *out);

#endif
```

- [ ] **Step 5: Write `search.c`**

```c
#include "search.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "passes.h"
#include "tacutil.h"
#include "timing.h"

typedef struct {
    TACList *list;
    int seq[SEARCH_MAX_SEQ];
    int len;
} Node;

SearchConfig search_default_config(void) {
    return (SearchConfig){ .max_depth = 4, .budget = 2000 };
}

static uint64_t hash_list(const TACList *l) {
    char *s = tac_list_to_string(l);
    uint64_t h = 1469598103934665603ULL;
    for (const char *p = s; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ULL;
    free(s);
    return h;
}

static int seen_contains(const uint64_t *seen, size_t n, uint64_t h) {
    for (size_t i = 0; i < n; i++)
        if (seen[i] == h) return 1;
    return 0;
}

/* Strictly better: lower cost, then fewer instructions, then shorter sequence. */
static int is_better(long cost, size_t ninstr, int seqlen,
                     long best_cost, size_t best_ninstr, int best_seqlen) {
    if (cost != best_cost) return cost < best_cost;
    if (ninstr != best_ninstr) return ninstr < best_ninstr;
    return seqlen < best_seqlen;
}

SearchResult search_best_sequence(TACList *input, const SearchConfig *cfg, const Env *initial) {
    SearchResult res;
    memset(&res, 0, sizeof res);
    double t0 = now_seconds();

    res.best = tac_copy_list(input);
    res.input_cost = interp_dynamic_cost(input, initial);
    res.best_cost = res.input_cost;

    TACList *base = apply_fixed_pipeline(input);
    res.baseline_cost = interp_dynamic_cost(base, initial);
    taclist_free(base);

    if (res.input_cost < 0) {          /* cannot score: return the input unchanged */
        res.baseline_cost = res.input_cost;
        res.seconds = now_seconds() - t0;
        return res;
    }

    size_t npasses = pass_count();
    int max_depth = cfg->max_depth > SEARCH_MAX_SEQ ? SEARCH_MAX_SEQ : cfg->max_depth;

    uint64_t *seen = malloc(sizeof(uint64_t) * ((size_t)cfg->budget + 2));
    size_t nseen = 0;
    seen[nseen++] = hash_list(input);

    Node *frontier = malloc(sizeof(Node));
    frontier[0].list = tac_copy_list(input);
    frontier[0].len = 0;
    size_t nf = 1;

    int stop = 0;
    for (int depth = 1; depth <= max_depth && nf > 0 && !stop; depth++) {
        Node *next = malloc(sizeof(Node) * nf * npasses);
        size_t nn = 0;

        for (size_t f = 0; f < nf && !stop; f++) {
            for (size_t p = 0; p < npasses; p++) {
                if (res.candidates >= cfg->budget) { stop = 1; break; }

                TACList *cand = PASS_REGISTRY[p].fn(frontier[f].list);
                uint64_t h = hash_list(cand);
                if (seen_contains(seen, nseen, h)) { taclist_free(cand); continue; }
                seen[nseen++] = h;
                res.candidates++;

                long cost = interp_dynamic_cost(cand, initial);
                if (cost < 0) {            /* a sound pass never does this */
                    res.invalid_candidates++;
                    taclist_free(cand);
                    continue;
                }

                Node n;
                n.list = cand;
                n.len = frontier[f].len + 1;
                memcpy(n.seq, frontier[f].seq, sizeof(int) * (size_t)frontier[f].len);
                n.seq[frontier[f].len] = (int)p;

                if (is_better(cost, cand->count, n.len,
                              res.best_cost, res.best->count, res.sequence_len)) {
                    taclist_free(res.best);
                    res.best = tac_copy_list(cand);
                    res.best_cost = cost;
                    res.sequence_len = n.len;
                    memcpy(res.sequence, n.seq, sizeof(int) * (size_t)n.len);
                }
                next[nn++] = n;
            }
        }

        for (size_t f = 0; f < nf; f++) taclist_free(frontier[f].list);
        free(frontier);
        frontier = next;
        nf = nn;
    }

    for (size_t f = 0; f < nf; f++) taclist_free(frontier[f].list);
    free(frontier);
    free(seen);

    res.seconds = now_seconds() - t0;
    return res;
}

void search_result_free(SearchResult *r) {
    taclist_free(r->best);
    r->best = NULL;
}

void search_print_sequence(const SearchResult *r, FILE *out) {
    if (r->sequence_len == 0) {
        fprintf(out, "(none)");
        return;
    }
    for (int i = 0; i < r->sequence_len; i++)
        fprintf(out, "%s%s", i ? ", " : "", PASS_REGISTRY[r->sequence[i]].name);
}
```

Note on `is_better` for the first candidate: `res.sequence_len` is 0 for the unmodified input, so a candidate with identical cost and instruction count is not "better" (it has a longer sequence), which is the intent.

- [ ] **Step 6: Run to verify it passes**

Run: `make test`
Expected: `ALL TESTS PASSED`

- [ ] **Step 7: Commit**

```bash
git add timing.h search.c search.h tests/test_search.c
git commit -m "feat: pass-ordering search with memoization and budget

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Driver options, `--search`, `--stats`, `--time`, ordering-sensitive samples

**Files:**
- Modify: `main.c` (full rewrite below)
- Create: `samples/order_copyprop_fold.mini`, `samples/order_strength_cse.mini`, `samples/order_loop_mix.mini`, `tests/test_samples.c`
- Modify: `tests/support.c` (extend `SAMPLE_FILES`)

**Interfaces:**
- Consumes: everything from Tasks 1-8.
- Produces: CLI flags `--search`, `--budget N`, `--stats`, `--time`, `--learn`, `--use-cache`, `--cache PATH`, `--explain`, `--fuzz N`, `--gen N`, `--seed S`, `--loo` parsed into `Options` (the last six are wired in Tasks 10-12; their anchors are marked). `--stats` prints parseable lines: `STAT <unoptimized|fixed|searched> <dynamic_cost> <static_ops>`, `STAT search_candidates <n>`, `STAT search_ms <x>`, and `VERIFY <fixed|searched> ok|MISMATCH`. `--time` prints `TIME <stage> <ms>` lines.

- [ ] **Step 1: Add the three ordering-sensitive samples**

`samples/order_copyprop_fold.mini` (copy propagation exposes a fold; fixed order finds nothing):

```
int x;
int y;
int z;

x = 4;
y = x;
z = y * 2;
```

`samples/order_strength_cse.mini` (strength reduction turns `a * 2` into `a + a`, which CSE then reuses; CSE first finds nothing):

```
int i;
int a;
int u;
int v;
int s;

a = 1;
i = 0;
s = 0;

while (i < 6) {
  a = a + i;
  u = a + a;
  v = a * 2;
  s = s + u + v;
  i = i + 1;
}
```

`samples/order_loop_mix.mini` (copyprop exposes an invariant multiply, strength reduces it, LICM hoists it):

```
int i;
int n;
int k;
int c;
int r;
int s;

n = 10;
k = 3;
i = 0;
s = 0;

while (i < n) {
  c = k;
  r = c * 2;
  s = s + r;
  i = i + 1;
}
```

In `tests/support.c` add the three new paths to `SAMPLE_FILES` (before the `NULL`).

- [ ] **Step 2: Write the failing test `tests/test_samples.c`**

```c
#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "interp.h"
#include "passes.h"
#include "pipeline.h"
#include "search.h"
#include "tacutil.h"

static void test_every_sample_is_clean_and_search_is_sound(void) {
    for (int i = 0; SAMPLE_FILES[i]; i++) {
        char *src = pipeline_read_file(SAMPLE_FILES[i]);
        CHECK(src != NULL);
        if (!src) continue;
        FrontEnd *fe = frontend_run(src);
        CHECK(fe->parse_errors == 0 && fe->semantic_errors == 0);

        SearchConfig cfg = search_default_config();
        SearchResult r = search_best_sequence(fe->tac, &cfg, NULL);
        InterpResult a = interp_run(fe->tac, NULL, INTERP_DEFAULT_STEP_LIMIT);
        InterpResult b = interp_run(r.best, NULL, INTERP_DEFAULT_STEP_LIMIT);
        int eq = interp_equivalent(&a, &b, fe->symtab);
        if (!eq) printf("  search result not equivalent on %s\n", SAMPLE_FILES[i]);
        CHECK(eq);
        CHECK(r.best_cost <= r.baseline_cost);
        CHECK(r.invalid_candidates == 0);
        interp_result_free(&a);
        interp_result_free(&b);
        search_result_free(&r);
        frontend_free(fe);
        free(src);
    }
}

static void test_ordering_sensitive_samples_improve(void) {
    const char *files[] = {
        "samples/order_copyprop_fold.mini",
        "samples/order_strength_cse.mini",
        "samples/order_loop_mix.mini",
        NULL
    };
    for (int i = 0; files[i]; i++) {
        char *src = pipeline_read_file(files[i]);
        FrontEnd *fe = frontend_run(src);
        SearchConfig cfg = search_default_config();
        SearchResult r = search_best_sequence(fe->tac, &cfg, NULL);
        if (r.best_cost >= r.baseline_cost)
            printf("  %s: searched %ld vs fixed %ld\n", files[i], r.best_cost, r.baseline_cost);
        CHECK(r.best_cost < r.baseline_cost);
        search_result_free(&r);
        frontend_free(fe);
        free(src);
    }
}

static void test_temp_name_collision_detected(void) {
    FrontEnd *fe = frontend_run("int t1; t1 = 2 * 3;");
    CHECK(frontend_has_temp_collision(fe->symtab));
    frontend_free(fe);
}

int main(void) {
    test_every_sample_is_clean_and_search_is_sound();
    test_ordering_sensitive_samples_improve();
    test_temp_name_collision_detected();
    TEST_END();
}
```

- [ ] **Step 3: Run to verify it fails or passes the library part**

Run: `make test`
Expected: the new samples exist so this should already pass; if `test_ordering_sensitive_samples_improve` fails for a sample, print the pass sequence with the Step 5 driver and adjust ONLY the sample program (not the passes) until the fixed pipeline is strictly worse than the search. Record which sequence wins for each sample in the commit message.

- [ ] **Step 4: Replace `main.c`**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include "codegen.h"
#include "optimizer.h"
#include "error_engine.h"
#include "cost.h"
#include "interp.h"
#include "passes.h"
#include "pipeline.h"
#include "search.h"
#include "tacutil.h"
#include "timing.h"

typedef struct {
    const char *path;
    int search, use_cache, learn, explain, stats, timing;
    int budget;
    const char *cache_path;
    int fuzz_n, gen_n, loo;
    unsigned seed;
    const char **loo_files;
    int loo_count;
} Options;

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [options] <source.mini>\n"
        "       %s --fuzz N [--seed S]\n"
        "       %s --gen N [--seed S]\n"
        "       %s --loo <a.mini> <b.mini> ...\n"
        "options: --search --budget N --learn --use-cache --cache PATH\n"
        "         --explain --stats --time\n",
        prog, prog, prog, prog);
}

static int parse_args(int argc, char **argv, Options *o) {
    memset(o, 0, sizeof *o);
    o->seed = 1;
    o->cache_path = ".minicc_cache";
    o->loo_files = malloc(sizeof(char *) * (size_t)(argc + 1));
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--search")) o->search = 1;
        else if (!strcmp(a, "--learn")) o->learn = 1;
        else if (!strcmp(a, "--use-cache")) o->use_cache = 1;
        else if (!strcmp(a, "--explain")) o->explain = 1;
        else if (!strcmp(a, "--stats")) o->stats = 1;
        else if (!strcmp(a, "--time")) o->timing = 1;
        else if (!strcmp(a, "--loo")) o->loo = 1;
        else if (!strcmp(a, "--budget") && i + 1 < argc) o->budget = atoi(argv[++i]);
        else if (!strcmp(a, "--cache") && i + 1 < argc) o->cache_path = argv[++i];
        else if (!strcmp(a, "--seed") && i + 1 < argc) o->seed = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--fuzz") && i + 1 < argc) o->fuzz_n = atoi(argv[++i]);
        else if (!strcmp(a, "--gen") && i + 1 < argc) o->gen_n = atoi(argv[++i]);
        else if (a[0] == '-' && a[1] == '-') { fprintf(stderr, "error: unknown option %s\n", a); return 0; }
        else if (o->loo) o->loo_files[o->loo_count++] = a;
        else if (!o->path) o->path = a;
        else return 0;
    }
    return 1;
}

static char *read_file(const char *path) {
    char *buf = pipeline_read_file(path);
    if (!buf) {
        fprintf(stderr, "error: could not open %s\n", path);
        exit(1);
    }
    return buf;
}

/* Returns the 1-indexed `line` from `source`, or "" if out of range.
 * Heap-allocated; caller frees. Used to give the error engine (and any
 * future LLM fallback) just the single offending source line. */
static char *extract_line(const char *source, int line) {
    int current = 1;
    const char *start = source;
    while (current < line && *start) {
        if (*start == '\n') current++;
        start++;
    }
    const char *end = start;
    while (*end && *end != '\n') end++;

    size_t len = (size_t)(end - start);
    char *out = malloc(len + 1);
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static void stat_row(const char *label, TACList *list) {
    long dyn = interp_dynamic_cost(list, NULL);
    printf("STAT %s %ld %ld\n", label, dyn, cost_static_ops(list));
}

static void verify_row(const char *label, TACList *ref, TACList *cand, const SymbolTable *symtab) {
    InterpResult a = interp_run(ref, NULL, INTERP_DEFAULT_STEP_LIMIT);
    InterpResult b = interp_run(cand, NULL, INTERP_DEFAULT_STEP_LIMIT);
    if (a.status != INTERP_OK)
        printf("VERIFY %s skipped (original does not run to completion)\n", label);
    else
        printf("VERIFY %s %s\n", label, interp_equivalent(&a, &b, symtab) ? "ok" : "MISMATCH");
    interp_result_free(&a);
    interp_result_free(&b);
}

int main(int argc, char **argv) {
    Options opt;
    if (!parse_args(argc, argv, &opt)) {
        usage(argv[0]);
        return 1;
    }

    /* TASK11-ANCHOR: --loo mode */
    /* TASK12-ANCHOR: --fuzz / --gen modes */

    if (!opt.path) {
        usage(argv[0]);
        return 1;
    }

    char *source = read_file(opt.path);

    /* --- Stage 1: Lexing --- */
    double t0 = now_seconds();
    TokenList *tokens = lex(source);
    double t_lex = now_seconds() - t0;
    printf("=== Tokens ===\n");
    for (size_t i = 0; i < tokens->count; i++) {
        Token *t = &tokens->items[i];
        printf("  %-12s '%s'  (line %d, col %d)\n",
               token_type_name(t->type), t->lexeme, t->line, t->col);
    }

    /* --- Stage 2: Parsing --- */
    codegen_reset_counters();
    ParseErrorList *parse_errors = parse_error_list_create();
    t0 = now_seconds();
    AstNode *ast = parse_program(tokens, parse_errors);
    double t_parse = now_seconds() - t0;

    printf("\n=== AST ===\n");
    ast_print(ast, 0);

    /* --- Stage 3: Semantic analysis --- */
    SemanticErrorList *semantic_errors = semantic_error_list_create();
    t0 = now_seconds();
    SymbolTable *symtab = analyze(ast, semantic_errors);
    double t_sem = now_seconds() - t0;

    printf("\n=== Symbol Table ===\n");
    symtab_print(symtab, stdout);

    /* --- Stage 4: TAC generation --- */
    t0 = now_seconds();
    TACList *tac = generate_tac(ast);
    double t_codegen = now_seconds() - t0;
    printf("\n=== TAC (unoptimized) ===\n");
    tac_print(tac, stdout);

    /* --- Stage 5: Optimization (constant folding, then LICM) --- */
    int collision = frontend_has_temp_collision(symtab);
    double t_fold = 0.0, t_licm = 0.0;
    TACList *tac_opt, *tac_licm;
    if (collision) {
        fprintf(stderr, "warning: a variable is named like a compiler temporary (t<N>); "
                        "optimizations are disabled for this program\n");
        tac_opt = tac_copy_list(tac);
        tac_licm = tac_copy_list(tac);
    } else {
        t0 = now_seconds();
        tac_opt = optimize_constant_folding(tac);
        t_fold = now_seconds() - t0;
        t0 = now_seconds();
        tac_licm = optimize_loop_invariant_code_motion(tac_opt);
        t_licm = now_seconds() - t0;
    }
    printf("\n=== TAC (optimized: constant folding) ===\n");
    tac_print(tac_opt, stdout);
    printf("\n=== TAC (optimized: + loop-invariant code motion) ===\n");
    tac_print(tac_licm, stdout);

    /* --- Stage 5b: search / cache / explain / stats (only with flags) --- */
    TACList *chosen = tac_licm;
    const char *chosen_label = "fixed";
    SearchResult sr;
    memset(&sr, 0, sizeof sr);
    int have_sr = 0;
    double t_search = 0.0;

    if (!collision && (opt.search || opt.learn)) {
        SearchConfig cfg = search_default_config();
        if (opt.budget > 0) cfg.budget = opt.budget;
        t0 = now_seconds();
        sr = search_best_sequence(tac, &cfg, NULL);
        t_search = now_seconds() - t0;
        have_sr = 1;
        chosen = sr.best;
        chosen_label = "searched";
        printf("\n=== TAC (searched: ");
        search_print_sequence(&sr, stdout);
        printf(") ===\n");
        tac_print(chosen, stdout);
    }

    /* TASK11-ANCHOR-B: --learn / --use-cache */
    /* TASK10-ANCHOR: --explain */

    if (!collision && opt.stats) {
        printf("\n=== Stats ===\n");
        cost_model_print(stdout);
        stat_row("unoptimized", tac);
        stat_row("fixed", tac_licm);
        verify_row("fixed", tac, tac_licm, symtab);
        if (chosen != tac_licm) {
            stat_row(chosen_label, chosen);
            verify_row(chosen_label, tac, chosen, symtab);
        }
        if (have_sr) {
            printf("STAT search_candidates %d\n", sr.candidates);
            printf("STAT search_ms %.3f\n", t_search * 1000.0);
        }
    }

    /* --- Stage 6: Errors + suggestions --- */
    if (parse_errors->count > 0 || semantic_errors->count > 0) {
        printf("\n=== Errors ===\n");
        for (size_t i = 0; i < parse_errors->count; i++) {
            ParseError *e = &parse_errors->items[i];
            char *line_text = extract_line(source, e->line);

            char *suggestion = error_engine_try_llm_suggestion(e, line_text);
            if (!suggestion) {
                suggestion = error_engine_suggest_parse(e);
            }
            printf("  [Parse]    line %d: %s\n", e->line, suggestion);

            free(suggestion);
            free(line_text);
        }
        for (size_t i = 0; i < semantic_errors->count; i++) {
            SemanticError *e = &semantic_errors->items[i];
            char *suggestion = error_engine_suggest_semantic(e);
            printf("  [Semantic] line %d: %s\n", e->line, suggestion);
            free(suggestion);
        }
    }

    if (opt.timing) {
        printf("\n=== Timing (ms) ===\n");
        printf("TIME lex %.3f\n", t_lex * 1000.0);
        printf("TIME parse %.3f\n", t_parse * 1000.0);
        printf("TIME semantic %.3f\n", t_sem * 1000.0);
        printf("TIME codegen %.3f\n", t_codegen * 1000.0);
        printf("TIME fold %.3f\n", t_fold * 1000.0);
        printf("TIME licm %.3f\n", t_licm * 1000.0);
        if (have_sr) printf("TIME search %.3f\n", t_search * 1000.0);
    }

    /* --- Cleanup --- */
    if (have_sr) search_result_free(&sr);
    taclist_free(tac_licm);
    taclist_free(tac_opt);
    taclist_free(tac);
    symtab_free(symtab);
    semantic_error_list_free(semantic_errors);
    ast_free(ast);
    parse_error_list_free(parse_errors);
    tokenlist_free(tokens);
    free(source);
    free(opt.loo_files);

    return 0;
}
```

- [ ] **Step 5: Build and check by hand**

Run:

```bash
make test && make && ./minicc samples/order_strength_cse.mini --search --stats | sed -n '/searched/,$p'
```

Expected: `ALL TESTS PASSED`; the output contains `=== TAC (searched: strength, cse, ... ) ===`, `STAT fixed <N> <M>`, `STAT searched <N'> <M'>` with `N' < N`, and `VERIFY searched ok`. Also confirm the default (no-flag) output is unchanged apart from the LICM change from Task 5: `./minicc samples/valid.mini | head -30`.

- [ ] **Step 6: Commit**

```bash
git add main.c samples/order_*.mini tests/test_samples.c tests/support.c
git commit -m "feat: driver options (--search --stats --time), ordering-sensitive samples

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Per-pass attribution trace (`--explain`)

**Files:**
- Create: `explain.h`, `explain.c`, `tests/test_explain.c`
- Modify: `main.c` (at `TASK10-ANCHOR`)

**Interfaces:**
- Consumes: `PASS_REGISTRY`, `interp_dynamic_cost`, `tac_instr_to_string`.
- Produces: `void explain_sequence(TACList *input, const int *seq, int len, const Env *initial, FILE *out)`. Output per step: `[<pass>] cost <before> -> <after> (<delta>)  instrs <n> -> <m>` followed by `   - <removed>` / `   + <added>` lines, then a `Saved per pass:` summary.

- [ ] **Step 1: Write the failing test `tests/test_explain.c`**

```c
#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "explain.h"
#include "passes.h"
#include "tacutil.h"

int main(void) {
    TACList *in = tac_from_text("t1 = a * b\nt2 = a * b\nx = t1 + t2\n");
    int seq[1] = { pass_index("cse") };

    FILE *f = capture_open();
    explain_sequence(in, seq, 1, NULL, f);
    char *text = capture_close(f);

    CHECK(strstr(text, "[cse]") != NULL);
    CHECK(strstr(text, "cost 7 -> 5") != NULL);          /* 3+3+1 -> 3+1+1 */
    CHECK(strstr(text, "- t2 = a * b") != NULL);
    CHECK(strstr(text, "+ t2 = t1") != NULL);
    CHECK(strstr(text, "cse saved 2") != NULL);
    free(text);

    /* empty sequence and empty program must not crash */
    TACList *empty = tac_from_text("");
    f = capture_open();
    explain_sequence(empty, NULL, 0, NULL, f);
    free(capture_close(f));
    taclist_free(empty);

    taclist_free(in);
    TEST_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `make test`
Expected: FAIL to build (`explain.h: No such file`).

- [ ] **Step 3: Write `explain.h` and `explain.c`**

`explain.h`:

```c
#ifndef EXPLAIN_H
#define EXPLAIN_H

#include <stdio.h>
#include "codegen.h"
#include "interp.h"

/* Applies PASS_REGISTRY[seq[0..len-1]] to `input` one at a time and prints,
 * for each step, the dynamic-cost change and an instruction-level diff,
 * then the total saved per pass. Costs use `initial` (may be NULL). */
void explain_sequence(TACList *input, const int *seq, int len,
                      const Env *initial, FILE *out);

#endif
```

`explain.c`:

```c
#include "explain.h"

#include <stdlib.h>
#include <string.h>

#include "passes.h"
#include "tacutil.h"

static char **lines_of(const TACList *l) {
    char **v = malloc(sizeof(char *) * (l->count + 1));
    for (size_t i = 0; i < l->count; i++) v[i] = tac_instr_to_string(&l->items[i]);
    return v;
}

static void free_lines(char **v, size_t n) {
    for (size_t i = 0; i < n; i++) free(v[i]);
    free(v);
}

#define DP(i, j) dp[(i) * (m + 1) + (j)]

static void print_diff(const TACList *a, const TACList *b, FILE *out) {
    size_t n = a->count, m = b->count;
    if ((n + 1) * (m + 1) > 4000000u) {
        fprintf(out, "   (diff skipped: program too large)\n");
        return;
    }
    char **sa = lines_of(a);
    char **sb = lines_of(b);
    unsigned *dp = calloc((n + 1) * (m + 1), sizeof(unsigned));

    for (size_t i = n; i-- > 0;) {
        for (size_t j = m; j-- > 0;) {
            if (strcmp(sa[i], sb[j]) == 0) DP(i, j) = DP(i + 1, j + 1) + 1;
            else DP(i, j) = DP(i + 1, j) >= DP(i, j + 1) ? DP(i + 1, j) : DP(i, j + 1);
        }
    }

    size_t i = 0, j = 0;
    while (i < n && j < m) {
        if (strcmp(sa[i], sb[j]) == 0) { i++; j++; }
        else if (DP(i + 1, j) >= DP(i, j + 1)) fprintf(out, "   - %s\n", sa[i++]);
        else fprintf(out, "   + %s\n", sb[j++]);
    }
    while (i < n) fprintf(out, "   - %s\n", sa[i++]);
    while (j < m) fprintf(out, "   + %s\n", sb[j++]);

    free(dp);
    free_lines(sa, n);
    free_lines(sb, m);
}

void explain_sequence(TACList *input, const int *seq, int len,
                      const Env *initial, FILE *out) {
    size_t np = pass_count();
    long *saved = calloc(np ? np : 1, sizeof(long));
    TACList *cur = tac_copy_list(input);

    for (int s = 0; s < len; s++) {
        const PassInfo *p = &PASS_REGISTRY[seq[s]];
        TACList *next = p->fn(cur);
        long before = interp_dynamic_cost(cur, initial);
        long after = interp_dynamic_cost(next, initial);
        fprintf(out, "[%s] cost %ld -> %ld (%+ld)  instrs %zu -> %zu\n",
                p->name, before, after, after - before, cur->count, next->count);
        print_diff(cur, next, out);
        if (before >= 0 && after >= 0) saved[seq[s]] += before - after;
        taclist_free(cur);
        cur = next;
    }

    fprintf(out, "Saved per pass:\n");
    for (size_t p = 0; p < np; p++)
        if (saved[p] != 0)
            fprintf(out, "  %s saved %ld\n", PASS_REGISTRY[p].name, saved[p]);

    taclist_free(cur);
    free(saved);
}
```

- [ ] **Step 4: Wire `--explain` into `main.c`**

Add `#include "explain.h"` with the other includes, and replace the line `/* TASK10-ANCHOR: --explain */` with:

```c
    if (!collision && opt.explain) {
        printf("\n=== Explain (%s) ===\n", chosen_label);
        if (have_sr) {
            explain_sequence(tac, sr.sequence, sr.sequence_len, NULL, stdout);
        } else {
            int fixed[2] = { pass_index("fold"), pass_index("licm") };
            explain_sequence(tac, fixed, 2, NULL, stdout);
        }
    }
```

- [ ] **Step 5: Run to verify it passes**

Run: `make test && make && ./minicc samples/order_loop_mix.mini --search --explain | sed -n '/=== Explain/,$p'`
Expected: `ALL TESTS PASSED`; the trace lists each pass in the winning sequence with a negative cost delta on at least one step and a `Saved per pass:` footer.

- [ ] **Step 6: Commit**

```bash
git add explain.c explain.h main.c tests/test_explain.c
git commit -m "feat: per-pass attribution trace (--explain)

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Feature-keyed heuristic cache (`--learn`, `--use-cache`, `--loo`)

**Files:**
- Create: `heuristic.h`, `heuristic.c`, `tests/test_heuristic.c`
- Modify: `main.c` (at `TASK11-ANCHOR` and `TASK11-ANCHOR-B`)

**Interfaces:**
- Consumes: `search_best_sequence`, `apply_sequence`, `apply_fixed_pipeline`, `frontend_run`, `pipeline_read_file`, `interp_dynamic_cost`, `StrMap`.
- Produces (`heuristic.h`):
  - `#define FEATURE_COUNT 7`; `typedef struct { int v[FEATURE_COUNT]; } Features;`
  - `Features heuristic_features(const TACList *list)` (quantized 0-5 per feature: instruction count, loop count, `*` count, `/` count, ifFalse count, foldable-binop count, copy count)
  - `typedef struct { Features f; int seq[SEARCH_MAX_SEQ]; int len; double ratio; } CacheEntry;`
  - `typedef struct { CacheEntry *items; size_t count, cap; } HeuristicCache;`
  - `HeuristicCache *cache_create(void)`, `HeuristicCache *cache_load(const char *path)` (missing file or bad lines → skipped), `int cache_save(const HeuristicCache *, const char *path)` (1 on success), `void cache_free(HeuristicCache *)`
  - `void cache_learn(HeuristicCache *, const Features *, const int *seq, int len, double ratio)` (same features: keep the lower ratio)
  - `const CacheEntry *cache_lookup(const HeuristicCache *, const Features *, int max_distance, int *distance_out)` (NULL on miss; ties go to the lower ratio)
  - `int feature_distance(const Features *a, const Features *b)` (L1)
  - `int heuristic_leave_one_out(const char **paths, int n, int max_distance, FILE *out)` (returns number of programs evaluated)
  - `#define CACHE_DEFAULT_MAX_DISTANCE 8`

- [ ] **Step 1: Write the failing test `tests/test_heuristic.c`**

```c
#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "heuristic.h"
#include "passes.h"
#include "tacutil.h"

#define CACHE_FILE "tests_tmp_cache.txt"

static void test_features(void) {
    TACList *empty = tac_from_text("");
    Features z = heuristic_features(empty);
    for (int i = 0; i < FEATURE_COUNT; i++) CHECK(z.v[i] == 0);
    taclist_free(empty);

    TACList *l = tac_from_text("i = 0\nL1:\nt1 = i < 5\nifFalse t1 goto L2\n"
                               "t2 = a * b\nt3 = 2 + 3\nx = t2\ngoto L1\nL2:\n");
    Features f = heuristic_features(l);
    CHECK(f.v[1] >= 1);   /* one loop (back-edge) */
    CHECK(f.v[2] >= 1);   /* one multiply */
    CHECK(f.v[4] >= 1);   /* one ifFalse */
    CHECK(f.v[5] >= 1);   /* one foldable binop */
    CHECK(f.v[6] >= 1);   /* one copy x = t2 */
    taclist_free(l);
}

static void test_learn_lookup(void) {
    HeuristicCache *c = cache_create();
    Features a = {{ 1, 0, 0, 0, 0, 0, 0 }};
    Features b = {{ 3, 1, 2, 0, 1, 0, 1 }};
    int s1[2] = { 2, 0 };
    int s2[1] = { 3 };
    cache_learn(c, &a, s1, 2, 0.8);
    cache_learn(c, &b, s2, 1, 0.5);

    int dist = -1;
    const CacheEntry *e = cache_lookup(c, &a, 8, &dist);
    CHECK(e != NULL && dist == 0 && e->len == 2 && e->seq[0] == 2);

    Features near_b = {{ 3, 1, 2, 0, 1, 0, 2 }};
    e = cache_lookup(c, &near_b, 8, &dist);
    CHECK(e != NULL && dist == 1 && e->len == 1 && e->seq[0] == 3);

    Features far = {{ 5, 5, 5, 5, 5, 5, 5 }};
    CHECK(cache_lookup(c, &far, 3, &dist) == NULL);

    /* same features: the lower ratio wins */
    int s3[1] = { 4 };
    cache_learn(c, &a, s3, 1, 0.9);   /* worse: ignored */
    e = cache_lookup(c, &a, 0, &dist);
    CHECK(e != NULL && e->len == 2);
    cache_learn(c, &a, s3, 1, 0.1);   /* better: replaces */
    e = cache_lookup(c, &a, 0, &dist);
    CHECK(e != NULL && e->len == 1 && e->seq[0] == 4);
    cache_free(c);
}

static void test_save_load_and_corruption(void) {
    HeuristicCache *c = cache_create();
    Features a = {{ 1, 0, 2, 0, 0, 0, 0 }};
    int s[3] = { pass_index("copyprop"), pass_index("fold"), pass_index("dce") };
    cache_learn(c, &a, s, 3, 0.25);
    CHECK(cache_save(c, CACHE_FILE));
    cache_free(c);

    /* append garbage lines: they must be skipped, valid ones kept */
    FILE *f = fopen(CACHE_FILE, "ab");
    fputs("this is not a cache line\n1 2 3\n9 9 9 9 9 9 9\tnosuchpass\t0.5\n", f);
    fclose(f);

    HeuristicCache *r = cache_load(CACHE_FILE);
    CHECK(r->count == 1);
    int dist;
    const CacheEntry *e = cache_lookup(r, &a, 0, &dist);
    CHECK(e != NULL && e->len == 3 && e->seq[2] == pass_index("dce"));
    CHECK(e->ratio > 0.24 && e->ratio < 0.26);
    cache_free(r);

    HeuristicCache *missing = cache_load("does_not_exist.cache");
    CHECK(missing != NULL && missing->count == 0);
    cache_free(missing);
    remove(CACHE_FILE);
}

static void test_leave_one_out_runs(void) {
    const char *files[] = {
        "samples/order_copyprop_fold.mini",
        "samples/order_strength_cse.mini",
        "samples/order_loop_mix.mini",
        "samples/loop_invariant.mini",
    };
    FILE *f = capture_open();
    int evaluated = heuristic_leave_one_out(files, 4, CACHE_DEFAULT_MAX_DISTANCE, f);
    char *text = capture_close(f);
    CHECK(evaluated == 4);
    CHECK(strstr(text, "hit rate") != NULL);
    free(text);
}

int main(void) {
    test_features();
    test_learn_lookup();
    test_save_load_and_corruption();
    test_leave_one_out_runs();
    TEST_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `make test`
Expected: FAIL to build (`heuristic.h: No such file`).

- [ ] **Step 3: Write `heuristic.h`**

```c
#ifndef HEURISTIC_H
#define HEURISTIC_H

#include <stddef.h>
#include <stdio.h>
#include "codegen.h"
#include "search.h"

#define FEATURE_COUNT 7
#define CACHE_DEFAULT_MAX_DISTANCE 8

/* Quantized (0..5) counts: instrs, loops, mul, div, ifFalse,
 * foldable binops, copies. */
typedef struct { int v[FEATURE_COUNT]; } Features;

typedef struct {
    Features f;
    int seq[SEARCH_MAX_SEQ];
    int len;
    double ratio;   /* searched cost / fixed-pipeline cost, lower is better */
} CacheEntry;

typedef struct {
    CacheEntry *items;
    size_t count;
    size_t cap;
} HeuristicCache;

Features heuristic_features(const TACList *list);
int feature_distance(const Features *a, const Features *b);

HeuristicCache *cache_create(void);
HeuristicCache *cache_load(const char *path);   /* missing file -> empty cache */
int cache_save(const HeuristicCache *c, const char *path);
void cache_free(HeuristicCache *c);

/* Same features already present: keep whichever has the lower ratio. */
void cache_learn(HeuristicCache *c, const Features *f, const int *seq, int len, double ratio);

/* Nearest entry within max_distance (ties: lower ratio), else NULL. */
const CacheEntry *cache_lookup(const HeuristicCache *c, const Features *f,
                               int max_distance, int *distance_out);

/* Leave-one-out evaluation over `paths`: for each program, train a cache on
 * all the OTHER programs and compile it from the cache alone. Prints one
 * row per program and a summary. Returns how many programs were evaluated. */
int heuristic_leave_one_out(const char **paths, int n, int max_distance, FILE *out);

#endif
```

- [ ] **Step 4: Write `heuristic.c`**

```c
#include "heuristic.h"

#include <stdlib.h>
#include <string.h>

#include "interp.h"
#include "passes.h"
#include "pipeline.h"
#include "strmap.h"
#include "tacutil.h"

static int bucket(long x) {
    if (x <= 0) return 0;
    if (x <= 2) return 1;
    if (x <= 5) return 2;
    if (x <= 10) return 3;
    if (x <= 20) return 4;
    return 5;
}

Features heuristic_features(const TACList *list) {
    long n_instr = 0, loops = 0, mul = 0, div = 0, branch = 0, foldable = 0, copies = 0;
    StrMap *labels = strmap_create();
    for (size_t i = 0; i < list->count; i++) {
        const TACInstr *in = &list->items[i];
        if (in->op == TAC_LABEL) { strmap_intern(labels, in->result); continue; }
        n_instr++;
        if (in->op == TAC_GOTO && strmap_find(labels, in->result) >= 0) loops++;  /* back-edge */
        if (in->op == TAC_IF_FALSE_GOTO) branch++;
        if (in->op == TAC_BINOP) {
            if (in->binop == TOK_STAR) mul++;
            if (in->binop == TOK_SLASH) div++;
            if (tac_is_numeric_literal(in->arg1) && tac_is_numeric_literal(in->arg2)) foldable++;
        }
        if (in->op == TAC_ASSIGN && !tac_is_numeric_literal(in->arg1)) copies++;
    }
    strmap_free(labels);

    Features f;
    f.v[0] = bucket(n_instr);
    f.v[1] = bucket(loops);
    f.v[2] = bucket(mul);
    f.v[3] = bucket(div);
    f.v[4] = bucket(branch);
    f.v[5] = bucket(foldable);
    f.v[6] = bucket(copies);
    return f;
}

int feature_distance(const Features *a, const Features *b) {
    int d = 0;
    for (int i = 0; i < FEATURE_COUNT; i++) d += abs(a->v[i] - b->v[i]);
    return d;
}

HeuristicCache *cache_create(void) {
    HeuristicCache *c = calloc(1, sizeof(HeuristicCache));
    return c;
}

void cache_free(HeuristicCache *c) {
    if (!c) return;
    free(c->items);
    free(c);
}

static void cache_push(HeuristicCache *c, CacheEntry e) {
    if (c->count == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 16;
        c->items = realloc(c->items, sizeof(CacheEntry) * c->cap);
    }
    c->items[c->count++] = e;
}

void cache_learn(HeuristicCache *c, const Features *f, const int *seq, int len, double ratio) {
    CacheEntry e;
    memset(&e, 0, sizeof e);
    e.f = *f;
    e.len = len > SEARCH_MAX_SEQ ? SEARCH_MAX_SEQ : len;
    if (e.len > 0) memcpy(e.seq, seq, sizeof(int) * (size_t)e.len);
    e.ratio = ratio;

    for (size_t i = 0; i < c->count; i++) {
        if (feature_distance(&c->items[i].f, f) == 0) {
            if (ratio < c->items[i].ratio) c->items[i] = e;
            return;
        }
    }
    cache_push(c, e);
}

const CacheEntry *cache_lookup(const HeuristicCache *c, const Features *f,
                               int max_distance, int *distance_out) {
    const CacheEntry *best = NULL;
    int best_d = 0;
    for (size_t i = 0; i < c->count; i++) {
        int d = feature_distance(&c->items[i].f, f);
        if (d > max_distance) continue;
        if (!best || d < best_d || (d == best_d && c->items[i].ratio < best->ratio)) {
            best = &c->items[i];
            best_d = d;
        }
    }
    if (best && distance_out) *distance_out = best_d;
    return best;
}

int cache_save(const HeuristicCache *c, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    for (size_t i = 0; i < c->count; i++) {
        const CacheEntry *e = &c->items[i];
        for (int k = 0; k < FEATURE_COUNT; k++) fprintf(f, "%d%s", e->f.v[k], k + 1 < FEATURE_COUNT ? " " : "");
        fputc('\t', f);
        if (e->len == 0) fputc('-', f);
        for (int k = 0; k < e->len; k++)
            fprintf(f, "%s%s", k ? "," : "", PASS_REGISTRY[e->seq[k]].name);
        fprintf(f, "\t%.4f\n", e->ratio);
    }
    fclose(f);
    return 1;
}

HeuristicCache *cache_load(const char *path) {
    HeuristicCache *c = cache_create();
    FILE *f = fopen(path, "rb");
    if (!f) return c;

    char line[1024];
    while (fgets(line, sizeof line, f)) {
        Features ft;
        char seqtxt[512];
        double ratio;
        int n = sscanf(line, "%d %d %d %d %d %d %d\t%511[^\t]\t%lf",
                       &ft.v[0], &ft.v[1], &ft.v[2], &ft.v[3], &ft.v[4], &ft.v[5], &ft.v[6],
                       seqtxt, &ratio);
        if (n != 9) continue;

        int seq[SEARCH_MAX_SEQ], len = 0, ok = 1;
        if (strcmp(seqtxt, "-") != 0) {
            for (char *tok = strtok(seqtxt, ","); tok; tok = strtok(NULL, ",")) {
                int idx = pass_index(tok);
                if (idx < 0 || len >= SEARCH_MAX_SEQ) { ok = 0; break; }
                seq[len++] = idx;
            }
        }
        if (!ok) continue;
        cache_learn(c, &ft, seq, len, ratio);
    }
    fclose(f);
    return c;
}

/* ---------- leave-one-out ---------- */

typedef struct {
    const char *path;
    FrontEnd *fe;
    Features f;
    int seq[SEARCH_MAX_SEQ];
    int len;
    long unopt, fixed, searched;
} Item;

int heuristic_leave_one_out(const char **paths, int n, int max_distance, FILE *out) {
    Item *items = calloc((size_t)(n > 0 ? n : 1), sizeof(Item));
    int usable = 0;

    for (int i = 0; i < n; i++) {
        items[i].path = paths[i];
        char *src = pipeline_read_file(paths[i]);
        if (!src) { fprintf(out, "skip %s (cannot open)\n", paths[i]); continue; }
        FrontEnd *fe = frontend_run(src);
        free(src);
        if (fe->parse_errors || fe->semantic_errors) {
            fprintf(out, "skip %s (front-end errors)\n", paths[i]);
            frontend_free(fe);
            continue;
        }
        items[i].fe = fe;
        items[i].f = heuristic_features(fe->tac);

        SearchConfig cfg = search_default_config();
        SearchResult r = search_best_sequence(fe->tac, &cfg, NULL);
        items[i].len = r.sequence_len;
        memcpy(items[i].seq, r.sequence, sizeof(int) * (size_t)r.sequence_len);
        items[i].unopt = r.input_cost;
        items[i].fixed = r.baseline_cost;
        items[i].searched = r.best_cost;
        search_result_free(&r);
        usable++;
    }

    fprintf(out, "%-36s %-10s %8s %8s %8s %8s\n",
            "program", "lookup", "unopt", "fixed", "cached", "searched");
    int evaluated = 0, hits = 0;
    double ratio_sum = 0.0;
    int ratio_n = 0;

    for (int i = 0; i < n; i++) {
        if (!items[i].fe) continue;
        HeuristicCache *c = cache_create();
        for (int j = 0; j < n; j++) {
            if (j == i || !items[j].fe) continue;
            double ratio = items[j].fixed > 0 ? (double)items[j].searched / (double)items[j].fixed : 1.0;
            cache_learn(c, &items[j].f, items[j].seq, items[j].len, ratio);
        }

        int dist = 0;
        const CacheEntry *e = cache_lookup(c, &items[i].f, max_distance, &dist);
        long cached_cost = items[i].fixed;
        char how[32];
        if (e) {
            TACList *applied = apply_sequence(items[i].fe->tac, e->seq, e->len);
            long cost = interp_dynamic_cost(applied, NULL);
            taclist_free(applied);
            if (cost >= 0) cached_cost = cost;
            snprintf(how, sizeof how, dist == 0 ? "exact" : "near(%d)", dist);
            hits++;
        } else {
            snprintf(how, sizeof how, "miss");
        }

        fprintf(out, "%-36s %-10s %8ld %8ld %8ld %8ld\n",
                items[i].path, how, items[i].unopt, items[i].fixed, cached_cost, items[i].searched);
        if (items[i].fixed > items[i].searched) {
            /* share of the search's gain over the fixed pipeline that the cache recovered */
            double gain = (double)(items[i].fixed - items[i].searched);
            double got = (double)(items[i].fixed - cached_cost);
            ratio_sum += got / gain;
            ratio_n++;
        }
        evaluated++;
        cache_free(c);
    }

    fprintf(out, "hit rate: %d/%d", hits, evaluated);
    if (ratio_n > 0)
        fprintf(out, "   search gain recovered by cache (programs where search beats fixed): %.0f%%",
                100.0 * ratio_sum / ratio_n);
    fprintf(out, "\n");

    for (int i = 0; i < n; i++) frontend_free(items[i].fe);
    free(items);
    (void)usable;
    return evaluated;
}
```

- [ ] **Step 5: Wire the modes into `main.c`**

Add `#include "heuristic.h"` with the other includes. Replace `/* TASK11-ANCHOR: --loo mode */` with:

```c
    if (opt.loo) {
        int n = heuristic_leave_one_out(opt.loo_files, opt.loo_count,
                                        CACHE_DEFAULT_MAX_DISTANCE, stdout);
        free(opt.loo_files);
        return n > 0 ? 0 : 1;
    }
```

Replace `/* TASK11-ANCHOR-B: --learn / --use-cache */` with:

```c
    if (!collision && opt.learn && have_sr) {
        HeuristicCache *cache = cache_load(opt.cache_path);
        Features ft = heuristic_features(tac);
        double ratio = sr.baseline_cost > 0 ? (double)sr.best_cost / (double)sr.baseline_cost : 1.0;
        cache_learn(cache, &ft, sr.sequence, sr.sequence_len, ratio);
        if (cache_save(cache, opt.cache_path))
            printf("\ncache: learned entry saved to %s (%zu entries)\n", opt.cache_path, cache->count);
        else
            fprintf(stderr, "warning: could not write cache file %s\n", opt.cache_path);
        cache_free(cache);
    }

    TACList *cached_result = NULL;
    if (!collision && opt.use_cache && !have_sr) {
        HeuristicCache *cache = cache_load(opt.cache_path);
        Features ft = heuristic_features(tac);
        int dist = 0;
        const CacheEntry *e = cache_lookup(cache, &ft, CACHE_DEFAULT_MAX_DISTANCE, &dist);
        if (e) {
            cached_result = apply_sequence(tac, e->seq, e->len);
            chosen = cached_result;
            chosen_label = "cached";
            printf("\n=== TAC (cache %s, distance %d: ", dist == 0 ? "exact" : "nearest", dist);
            for (int i = 0; i < e->len; i++)
                printf("%s%s", i ? ", " : "", PASS_REGISTRY[e->seq[i]].name);
            printf("%s) ===\n", e->len == 0 ? "(none)" : "");
            tac_print(chosen, stdout);
        } else {
            printf("\ncache: miss, using the fixed pipeline\n");
        }
        cache_free(cache);
    }
```

Also, in the `--explain` block added in Task 10, make the cached case explain the cached sequence is out of scope: no change needed (it falls back to the fixed pipeline unless `--search` was given). In cleanup, add `taclist_free(cached_result);` before `taclist_free(tac_licm);`.

- [ ] **Step 6: Run to verify it passes**

Run:

```bash
make test && make \
 && rm -f .minicc_cache \
 && for f in samples/order_*.mini samples/loop_invariant.mini; do ./minicc --learn "$f" > /dev/null; done \
 && ./minicc --use-cache --stats samples/order_loop_mix.mini | sed -n '/=== Stats/,$p' \
 && ./minicc --loo samples/order_*.mini samples/loop_*.mini samples/valid.mini
```

Expected: `ALL TESTS PASSED`; `.minicc_cache` gains one line per program; `--use-cache` prints `cache exact, distance 0` and `STAT cached <cost>` equal to the searched cost; `--loo` prints one row per program and a `hit rate:` summary.

- [ ] **Step 7: Commit**

```bash
git add heuristic.c heuristic.h main.c tests/test_heuristic.c
git commit -m "feat: feature-keyed heuristic cache with leave-one-out evaluation

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 12: Differential fuzzer and program generator (`--fuzz`, `--gen`)

**Files:**
- Create: `fuzz.h`, `fuzz.c`, `tests/test_fuzz.c`
- Modify: `main.c` (at `TASK12-ANCHOR`)

**Interfaces:**
- Consumes: `frontend_run`, `PASS_REGISTRY`, `apply_fixed_pipeline`, `apply_sequence`, `search_best_sequence`, `interp_run`, `interp_equivalent`.
- Produces (`fuzz.h`):
  - `char *fuzz_generate_program(unsigned seed, int chunks)` (heap source text; `chunks` ≈ top-level statements; int vars `v0..v3`, float vars `f0..f1`, loop counters `c0..c5`)
  - `typedef struct { int programs, rejected, checks, skipped, failures; } FuzzStats;`
  - `FuzzStats fuzz_run(int n, unsigned seed, int envs_per_program, const PassInfo *extra, FILE *out)`: generates `n` programs (program `i` uses seed `seed + i * 7919`), checks each pass alone, the fixed pipeline, all passes twice in registry order, and a budget-limited searched sequence (and `*extra` if non-NULL) against the unoptimized TAC under `envs_per_program` random initial environments. A case whose original run does not finish `INTERP_OK` is counted in `skipped`. On a failure it prints the seed, the program source, the candidate label, and the differing variables.

- [ ] **Step 1: Write the failing test `tests/test_fuzz.c`**

```c
#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "fuzz.h"
#include "passes.h"
#include "pipeline.h"
#include "tacutil.h"

/* A deliberately wrong pass: turns every '+' into '-'. */
static TACList *broken_plus_to_minus(TACList *input) {
    TACList *out = tac_copy_list(input);
    for (size_t i = 0; i < out->count; i++)
        if (out->items[i].op == TAC_BINOP && out->items[i].binop == TOK_PLUS)
            out->items[i].binop = TOK_MINUS;
    return out;
}

static void test_generator_output_is_mostly_accepted(void) {
    int accepted = 0, total = 40;
    for (int i = 0; i < total; i++) {
        char *src = fuzz_generate_program(1000u + (unsigned)i, 6);
        FrontEnd *fe = frontend_run(src);
        if (fe->parse_errors == 0 && fe->semantic_errors == 0) accepted++;
        frontend_free(fe);
        free(src);
    }
    if (accepted * 10 < total * 9) printf("  only %d/%d generated programs accepted\n", accepted, total);
    CHECK(accepted * 10 >= total * 9);   /* at least 90% */
}

static void test_generator_is_deterministic(void) {
    char *a = fuzz_generate_program(7, 5);
    char *b = fuzz_generate_program(7, 5);
    char *c = fuzz_generate_program(8, 5);
    CHECK(strcmp(a, b) == 0);
    CHECK(strcmp(a, c) != 0);
    free(a); free(b); free(c);
}

static void test_correct_passes_survive_fuzzing(void) {
    FILE *f = capture_open();
    FuzzStats s = fuzz_run(60, 1, 4, NULL, f);
    char *log = capture_close(f);
    if (s.failures) printf("%s", log);
    CHECK(s.failures == 0);
    CHECK(s.checks > 200);
    CHECK(s.rejected * 10 <= s.programs);
    free(log);
}

static void test_fuzzer_catches_a_broken_pass(void) {
    PassInfo broken = { "broken", broken_plus_to_minus };
    FILE *f = capture_open();
    FuzzStats s = fuzz_run(40, 1, 3, &broken, f);
    char *log = capture_close(f);
    CHECK(s.failures > 0);
    CHECK(strstr(log, "broken") != NULL);
    free(log);
}

int main(void) {
    test_generator_output_is_mostly_accepted();
    test_generator_is_deterministic();
    test_correct_passes_survive_fuzzing();
    test_fuzzer_catches_a_broken_pass();
    TEST_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `make test`
Expected: FAIL to build (`fuzz.h: No such file`).

- [ ] **Step 3: Write `fuzz.h`**

```c
#ifndef FUZZ_H
#define FUZZ_H

#include <stdio.h>
#include "passes.h"

typedef struct {
    int programs;   /* generated */
    int rejected;   /* front-end errors, not checked */
    int checks;     /* (program, env, candidate) comparisons performed */
    int skipped;    /* environments where the original run did not finish OK */
    int failures;   /* candidates not equivalent to the original */
} FuzzStats;

/* Valid, closed, terminating mini-language source text. Same (seed, chunks)
 * always yields the same text. Caller frees. */
char *fuzz_generate_program(unsigned seed, int chunks);

/* See the plan: differential check of each pass, the fixed pipeline, all
 * passes twice, the searched sequence, and `extra` (may be NULL). Report
 * lines go to `out`. */
FuzzStats fuzz_run(int n, unsigned seed, int envs_per_program,
                   const PassInfo *extra, FILE *out);

#endif
```

- [ ] **Step 4: Write `fuzz.c`**

```c
#include "fuzz.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "interp.h"
#include "pipeline.h"
#include "search.h"
#include "tacutil.h"

/* ---------- tiny RNG and string buffer ---------- */

typedef struct { unsigned s; } Rng;

static unsigned rng_next(Rng *r) {
    r->s = r->s * 1103515245u + 12345u;
    return (r->s >> 16) & 0x7fffu;
}

static int rng_range(Rng *r, int n) { return (int)(rng_next(r) % (unsigned)n); }

typedef struct { char *s; size_t len, cap; } Buf;

static void buf_printf(Buf *b, const char *fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    size_t n = strlen(tmp);
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->s = realloc(b->s, b->cap);
    }
    memcpy(b->s + b->len, tmp, n + 1);
    b->len += n;
}

/* ---------- program generator ---------- */

#define NI 4        /* int vars v0..v3 */
#define NF 2        /* float vars f0..f1 */
#define NLOOPS 6    /* loop counters c0..c5 */

static void gen_int_expr(Rng *r, Buf *b, int depth) {
    if (depth == 0 || rng_range(r, 3) == 0) {
        if (rng_range(r, 2)) buf_printf(b, "v%d", rng_range(r, NI));
        else buf_printf(b, "%d", rng_range(r, 10));
        return;
    }
    int k = rng_range(r, 4);   /* + - * / */
    buf_printf(b, "(");
    gen_int_expr(r, b, depth - 1);
    if (k == 3) {
        buf_printf(b, " / %d", 1 + rng_range(r, 9));   /* nonzero literal divisor */
    } else {
        buf_printf(b, " %c ", "+-*"[k]);
        gen_int_expr(r, b, depth - 1);
    }
    buf_printf(b, ")");
}

static void gen_float_expr(Rng *r, Buf *b, int depth) {
    if (depth == 0 || rng_range(r, 3) == 0) {
        if (rng_range(r, 2)) buf_printf(b, "f%d", rng_range(r, NF));
        else buf_printf(b, "%d.5", rng_range(r, 10));
        return;
    }
    int k = rng_range(r, 4);
    buf_printf(b, "(");
    gen_float_expr(r, b, depth - 1);
    if (k == 3) {
        buf_printf(b, " / %d.0", 1 + rng_range(r, 9));
    } else {
        buf_printf(b, " %c ", "+-*"[k]);
        gen_float_expr(r, b, depth - 1);
    }
    buf_printf(b, ")");
}

static void gen_cond(Rng *r, Buf *b) {
    static const char *ops[] = { "<", ">", "<=", ">=", "==" };
    const char *op = ops[rng_range(r, 5)];
    if (rng_range(r, 3) == 0) {
        gen_float_expr(r, b, 1);
        buf_printf(b, " %s ", op);
        gen_float_expr(r, b, 1);
    } else {
        gen_int_expr(r, b, 2);
        buf_printf(b, " %s ", op);
        gen_int_expr(r, b, 2);
    }
}

static void gen_stmt(Rng *r, Buf *b, int depth, int *loops);

static void gen_block(Rng *r, Buf *b, int depth, int *loops) {
    int n = 1 + rng_range(r, 3);
    for (int i = 0; i < n; i++) gen_stmt(r, b, depth, loops);
}

static void gen_stmt(Rng *r, Buf *b, int depth, int *loops) {
    int k = rng_range(r, depth < 2 ? 7 : 3);
    if (k <= 1) {
        buf_printf(b, "v%d = ", rng_range(r, NI));
        gen_int_expr(r, b, 3);
        buf_printf(b, ";\n");
    } else if (k == 2) {
        buf_printf(b, "f%d = ", rng_range(r, NF));
        gen_float_expr(r, b, 3);
        buf_printf(b, ";\n");
    } else if (k <= 4) {
        buf_printf(b, "if (");
        gen_cond(r, b);
        buf_printf(b, ") {\n");
        gen_block(r, b, depth + 1, loops);
        if (rng_range(r, 2)) {
            buf_printf(b, "} else {\n");
            gen_block(r, b, depth + 1, loops);
        }
        buf_printf(b, "}\n");
    } else if (*loops < NLOOPS) {
        int id = (*loops)++;
        buf_printf(b, "c%d = 0;\nwhile (c%d < %d) {\n", id, id, 1 + rng_range(r, 6));
        gen_block(r, b, depth + 1, loops);
        buf_printf(b, "c%d = c%d + 1;\n}\n", id, id);
    } else {
        buf_printf(b, "v%d = ", rng_range(r, NI));
        gen_int_expr(r, b, 2);
        buf_printf(b, ";\n");
    }
}

char *fuzz_generate_program(unsigned seed, int chunks) {
    Rng r = { seed * 2654435761u + 1u };
    Buf b = { NULL, 0, 0 };
    buf_printf(&b, "");   /* allocate */

    for (int i = 0; i < NI; i++) buf_printf(&b, "int v%d;\n", i);
    for (int i = 0; i < NF; i++) buf_printf(&b, "float f%d;\n", i);
    for (int i = 0; i < NLOOPS; i++) buf_printf(&b, "int c%d;\n", i);
    for (int i = 0; i < NI; i++)
        if (rng_range(&r, 2)) buf_printf(&b, "v%d = %d;\n", i, rng_range(&r, 10));

    int loops = 0;
    for (int i = 0; i < chunks; i++) gen_stmt(&r, &b, 0, &loops);
    return b.s;
}

/* ---------- differential checker ---------- */

typedef struct { char *label; TACList *list; } Candidate;

static Env *random_env(const SymbolTable *sym, Rng *r) {
    Env *e = env_create();
    for (size_t i = 0; i < sym->count; i++) {
        const SymbolEntry *s = &sym->items[i];
        if (s->type == TYPE_FLOAT) env_set(e, s->name, value_float((double)(rng_range(r, 9) - 4) + 0.5));
        else env_set(e, s->name, value_int(rng_range(r, 9) - 4));
    }
    return e;
}

static void print_value(FILE *out, Value v) {
    if (v.kind == VAL_INT) fprintf(out, "%ld", v.i);
    else fprintf(out, "%g", v.f);
}

static void report_failure(FILE *out, unsigned seed, const char *src, const char *label,
                           const InterpResult *ref, const InterpResult *got,
                           const SymbolTable *sym) {
    fprintf(out, "FAIL seed=%u candidate=%s status=%d\n", seed, label, (int)got->status);
    for (size_t i = 0; i < sym->count; i++) {
        const char *name = sym->items[i].name;
        Value a = value_int(0), b = value_int(0);
        env_get(ref->final_env, name, &a);
        env_get(got->final_env, name, &b);
        if (a.kind != b.kind || (a.kind == VAL_INT ? a.i != b.i : a.f != b.f)) {
            fprintf(out, "  %s: expected ", name);
            print_value(out, a);
            fprintf(out, " got ");
            print_value(out, b);
            fprintf(out, "\n");
        }
    }
    fprintf(out, "--- program ---\n%s---------------\n", src);
}

FuzzStats fuzz_run(int n, unsigned seed, int envs_per_program,
                   const PassInfo *extra, FILE *out) {
    FuzzStats st = { 0, 0, 0, 0, 0 };
    const long FUZZ_STEP_LIMIT = 200000;

    for (int pi = 0; pi < n; pi++) {
        unsigned pseed = seed + (unsigned)pi * 7919u;
        char *src = fuzz_generate_program(pseed, 6);
        st.programs++;

        FrontEnd *fe = frontend_run(src);
        if (fe->parse_errors || fe->semantic_errors || frontend_has_temp_collision(fe->symtab)) {
            st.rejected++;
            frontend_free(fe);
            free(src);
            continue;
        }

        /* candidates: each pass alone, fixed pipeline, all passes twice, searched, extra */
        size_t np = pass_count();
        size_t cap = np + 5;
        Candidate *cands = calloc(cap, sizeof(Candidate));
        size_t nc = 0;

        for (size_t p = 0; p < np; p++) {
            cands[nc].label = tac_dup_str(PASS_REGISTRY[p].name);
            cands[nc++].list = PASS_REGISTRY[p].fn(fe->tac);
        }
        cands[nc].label = tac_dup_str("fixed");
        cands[nc++].list = apply_fixed_pipeline(fe->tac);

        int all[2 * 16];
        int alen = 0;
        for (int rep = 0; rep < 2; rep++)
            for (size_t p = 0; p < np && alen < 2 * 16; p++) all[alen++] = (int)p;
        cands[nc].label = tac_dup_str("all-twice");
        cands[nc++].list = apply_sequence(fe->tac, all, alen);

        SearchConfig cfg = search_default_config();
        cfg.budget = 150;
        SearchResult sr = search_best_sequence(fe->tac, &cfg, NULL);
        cands[nc].label = tac_dup_str("searched");
        cands[nc++].list = tac_copy_list(sr.best);
        search_result_free(&sr);

        if (extra) {
            cands[nc].label = tac_dup_str(extra->name);
            cands[nc++].list = extra->fn(fe->tac);
        }

        Rng erng = { pseed ^ 0x9e3779b9u };
        for (int e = 0; e < envs_per_program; e++) {
            Env *env = random_env(fe->symtab, &erng);
            InterpResult ref = interp_run(fe->tac, env, FUZZ_STEP_LIMIT);
            if (ref.status != INTERP_OK) {
                st.skipped++;
                interp_result_free(&ref);
                env_free(env);
                continue;
            }
            for (size_t c = 0; c < nc; c++) {
                InterpResult got = interp_run(cands[c].list, env, FUZZ_STEP_LIMIT);
                st.checks++;
                if (!interp_equivalent(&ref, &got, fe->symtab)) {
                    st.failures++;
                    report_failure(out, pseed, src, cands[c].label, &ref, &got, fe->symtab);
                }
                interp_result_free(&got);
            }
            interp_result_free(&ref);
            env_free(env);
        }

        for (size_t c = 0; c < nc; c++) {
            free(cands[c].label);
            taclist_free(cands[c].list);
        }
        free(cands);
        frontend_free(fe);
        free(src);
    }

    fprintf(out, "fuzz: %d programs (%d rejected by front end), %d checks, %d skipped, %d failures\n",
            st.programs, st.rejected, st.checks, st.skipped, st.failures);
    return st;
}
```

- [ ] **Step 5: Wire the modes into `main.c`**

Add `#include "fuzz.h"` with the other includes and replace `/* TASK12-ANCHOR: --fuzz / --gen modes */` with:

```c
    if (opt.gen_n > 0) {
        char *src = fuzz_generate_program(opt.seed, opt.gen_n);
        fputs(src, stdout);
        free(src);
        free(opt.loo_files);
        return 0;
    }
    if (opt.fuzz_n > 0) {
        FuzzStats fs = fuzz_run(opt.fuzz_n, opt.seed, 4, NULL, stdout);
        free(opt.loo_files);
        return fs.failures > 0 ? 1 : 0;
    }
```

- [ ] **Step 6: Run to verify it passes; fix real bugs if the fuzzer finds any**

Run: `make test`
Expected: `ALL TESTS PASSED`.

If `test_generator_output_is_mostly_accepted` fails, run `./minicc --gen 6 --seed 1000 > /tmp/g.mini; ./minicc /tmp/g.mini | sed -n '/Errors/,$p'` to see what the front end rejects, and adjust the generator (not the front end) until at least 90% are accepted.

If `test_correct_passes_survive_fuzzing` reports a `FAIL`, the fuzzer found a real pass bug: the report names the candidate pass and prints the program. Do not weaken the test. Reproduce with `./minicc --fuzz 60 --seed 1`, minimize by hand, fix the pass, add a regression case to `tests/test_passes.c`, and re-run.

- [ ] **Step 7: Run a larger campaign once**

Run: `./minicc --fuzz 500 --seed 42 | tail -3`
Expected: `fuzz: 500 programs (...), ... checks, ... skipped, 0 failures`

- [ ] **Step 8: Commit**

```bash
git add fuzz.c fuzz.h main.c tests/test_fuzz.c
git commit -m "feat: program generator and differential fuzzer (--fuzz, --gen)

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 13: Compile-time profiling and the LICM hot-spot fix

**Files:**
- Create: `bench/compile_time.sh`
- Modify: `optimizer.c` (rewrite the LICM helpers), `tests/test_licm.c` (add a scale test)

**Interfaces:**
- Consumes: `--time`, `--gen`, `StrMap`.
- Produces: `optimize_loop_invariant_code_motion` with unchanged behaviour but linear in program size (per-loop assign-count map and a label map instead of rescanning).

- [ ] **Step 1: Write the timing script `bench/compile_time.sh`**

```bash
#!/usr/bin/env bash
# Usage: bench/compile_time.sh [N ...]   (generated-program sizes, default: 500 1000 2000 4000)
# Prints per-stage compile time in ms for generated programs of N top-level
# statements, with and without --search. Sample programs compile in
# microseconds, so scaling is measured on generated inputs.
set -euo pipefail
BIN=${BIN:-./minicc}
SIZES=("$@")
[ ${#SIZES[@]} -eq 0 ] && SIZES=(500 1000 2000 4000)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

printf "%-8s %-9s %9s %9s %9s %9s %9s %10s\n" N mode lex parse codegen fold licm search
for n in "${SIZES[@]}"; do
  "$BIN" --gen "$n" --seed 1 > "$TMP/big.mini"
  for mode in fixed search; do
    flags="--time"
    [ "$mode" = search ] && flags="--time --search --budget 300"
    out=$("$BIN" $flags "$TMP/big.mini" | tr -d '\r')
    t() { echo "$out" | awk -v k="$1" '$1=="TIME" && $2==k {print $3}'; }
    printf "%-8s %-9s %9s %9s %9s %9s %9s %10s\n" "$n" "$mode" \
      "$(t lex)" "$(t parse)" "$(t codegen)" "$(t fold)" "$(t licm)" "$(t search)"
  done
done
```

Run `chmod +x bench/compile_time.sh`.

- [ ] **Step 2: Record the baseline**

Run: `make && bench/compile_time.sh 500 1000 2000 4000 | tee /tmp/licm_before.txt`
Expected: a table. Note the `licm` column at N=1000 vs N=2000 vs N=4000. If it more than triples when N doubles, LICM is quadratic or worse. (If no column dominates, record the table anyway and keep Step 3, which is a correctness-neutral cleanup.)

- [ ] **Step 3: Add the scale test to `tests/test_licm.c`**

Add before `main`, and call it from `main`:

```c
/* Behaviour is unchanged on a large input and completes quickly. A body of
 * 2000 independent invariant temps must all be hoisted, in order. */
static void test_large_loop(void) {
    char *text = malloc(200000);
    size_t len = 0;
    len += sprintf(text + len, "i = 0\nL1:\nt1 = i < 3\nifFalse t1 goto L2\n");
    for (int k = 0; k < 2000; k++)
        len += sprintf(text + len, "t%d = a * b\nx%d = t%d\n", 100 + k, k, 100 + k);
    len += sprintf(text + len, "t9 = i + 1\ni = t9\ngoto L1\nL2:\n");

    TACList *in = tac_from_text(text);
    TACList *out = optimize_loop_invariant_code_motion(in);
    CHECK(out->count == in->count);
    /* the first 2000 instructions are the hoisted preheader (after `i = 0`) */
    CHECK(out->items[1].op == TAC_BINOP);
    CHECK(strcmp(out->items[1].result, "t100") == 0);
    CHECK(strcmp(out->items[2000].result, "t2099") == 0);
    CHECK(out->items[2001].op == TAC_LABEL);
    taclist_free(in);
    taclist_free(out);
    free(text);
}
```

- [ ] **Step 4: Rewrite the LICM bookkeeping**

In `optimizer.c`, add `#include "strmap.h"`. Delete `name_assign_count` and `find_if_false_after` is kept. Replace the whole `optimize_loop_invariant_code_motion` function with the version below (it keeps the Task 5 guards and the two-pass structure; only the lookups change):

```c
TACList *optimize_loop_invariant_code_motion(TACList *input) {
    TACList *output = taclist_create();
    if (input->count == 0) return output;

    char *hoisted = calloc(input->count, sizeof(char));
    LoopInfo *loops = malloc(sizeof(LoopInfo) * (input->count + 1));
    size_t loop_count = 0;

    /* label name -> index of its TAC_LABEL */
    StrMap *labels = strmap_create();
    size_t *label_pos = malloc(sizeof(size_t) * (input->count + 1));
    for (size_t i = 0; i < input->count; i++) {
        if (input->items[i].op != TAC_LABEL) continue;
        int before = (int)strmap_count(labels);
        int id = strmap_intern(labels, input->items[i].result);
        if (id == before) label_pos[id] = i;
    }

    /* Pass 1: find every loop (a backward TAC_GOTO is the back edge) and,
     * for each straight-line body, mark which instructions are safe to
     * hoist into a preheader. */
    for (size_t i = 0; i < input->count; i++) {
        if (input->items[i].op != TAC_GOTO) continue;

        int label_id = strmap_find(labels, input->items[i].result);
        if (label_id < 0 || label_pos[label_id] >= i) continue; /* forward goto (if/else) */
        size_t header = label_pos[label_id];

        size_t if_false = find_if_false_after(input, header + 1);
        if (if_false >= i) continue;

        size_t body_start = if_false + 1;
        size_t body_end = i;
        if (body_has_control_flow(input, body_start, body_end)) continue;

        /* One pass over the body: how many times is each name written? */
        StrMap *defs = strmap_create();
        int *def_count = calloc(body_end - body_start + 1, sizeof(int));
        for (size_t k = body_start; k < body_end; k++) {
            TACInstr *bi = &input->items[k];
            if (bi->op != TAC_ASSIGN && bi->op != TAC_BINOP) continue;
            int before = (int)strmap_count(defs);
            int id = strmap_intern(defs, bi->result);
            if (id == before) def_count[id] = 0;
            def_count[id]++;
        }
        StrMap *hoisted_defs = strmap_create();

        for (size_t k = body_start; k < body_end; k++) {
            TACInstr *bi = &input->items[k];
            if (bi->op != TAC_ASSIGN && bi->op != TAC_BINOP) continue;
            if (!tac_is_temp_name(bi->result)) continue;
            if (bi->op == TAC_BINOP && bi->binop == TOK_SLASH &&
                !(tac_is_numeric_literal(bi->arg2) && atof(bi->arg2) != 0.0)) continue;
            int rid = strmap_find(defs, bi->result);
            if (rid < 0 || def_count[rid] != 1) continue;

            int invariant = 1;
            const char *operands[2] = { bi->arg1, bi->arg2 };
            for (int oi = 0; oi < 2 && invariant; oi++) {
                const char *val = operands[oi];
                if (!val || tac_is_numeric_literal(val)) continue;
                if (strmap_find(defs, val) < 0) continue;            /* defined outside the loop */
                if (strmap_find(hoisted_defs, val) < 0) invariant = 0; /* defined here, not hoisted */
            }
            if (invariant) {
                hoisted[k] = 1;
                strmap_intern(hoisted_defs, bi->result);
            }
        }

        strmap_free(hoisted_defs);
        free(def_count);
        strmap_free(defs);

        loops[loop_count++] = (LoopInfo){ .header = header, .body_start = body_start, .body_end = body_end };
    }

    /* Pass 2: emit, inserting each loop's hoisted instructions as a
     * preheader right before its header label. */
    size_t next_loop = 0;
    for (size_t i = 0; i < input->count; i++) {
        if (next_loop < loop_count && i == loops[next_loop].header) {
            for (size_t k = loops[next_loop].body_start; k < loops[next_loop].body_end; k++) {
                if (hoisted[k]) taclist_push(output, tac_copy_instr(&input->items[k]));
            }
            next_loop++;
        }
        if (hoisted[i]) continue;
        taclist_push(output, tac_copy_instr(&input->items[i]));
    }

    strmap_free(labels);
    free(label_pos);
    free(loops);
    free(hoisted);
    return output;
}
```

Pass 2's `next_loop` walk assumes `loops[]` is ordered by `header`; the old code relied on the same property (loops are discovered in order of their back-edges, and these programs have only properly nested or sequential loops with straight-line bodies, so headers ascend). Keep that assumption and note it in the function comment.

- [ ] **Step 5: Verify behaviour is unchanged and measure**

Run:

```bash
make test && make && bench/compile_time.sh 500 1000 2000 4000 | tee /tmp/licm_after.txt && ./minicc --fuzz 200 --seed 3 | tail -1
```

Expected: `ALL TESTS PASSED`; the fuzz summary reports `0 failures`; the `licm` column grows roughly linearly (about 2x per doubling). Save both tables into `REPORT.md` in Task 15 (before and after).

- [ ] **Step 6: Commit**

```bash
git add bench/compile_time.sh optimizer.c tests/test_licm.c
git commit -m "perf: linear-time LICM bookkeeping and compile-time benchmark script

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 14: Benchmark table and optional GCC side table

**Files:**
- Create: `bench/run_bench.sh`, `bench/gcc_compare.sh`, `bench/c/loop_invariant.c`, `bench/c/loop_invariant_chain.c`, `bench/c/loop_no_invariant.c`, `bench/c/order_strength_cse.c`, `bench/c/order_loop_mix.c`

**Interfaces:**
- Consumes: `./minicc --search --stats` output lines (`STAT`, `VERIFY`).
- Produces: a Markdown results table on stdout; non-zero exit status if any `VERIFY ... MISMATCH` appears.

- [ ] **Step 1: Write `bench/run_bench.sh`**

```bash
#!/usr/bin/env bash
# Usage: bench/run_bench.sh [program.mini ...]
# Markdown table of weighted dynamic cost: unoptimized vs fixed pipeline vs
# searched, plus search effort. Exits 1 if any optimized program differs
# from the original (VERIFY ... MISMATCH).
set -euo pipefail
BIN=${BIN:-./minicc}
if [ "$#" -gt 0 ]; then
  FILES=("$@")
else
  FILES=(samples/valid.mini samples/constant_folding.mini
         samples/loop_invariant.mini samples/loop_invariant_chain.mini
         samples/loop_no_invariant.mini samples/loop_with_if.mini
         samples/order_copyprop_fold.mini samples/order_strength_cse.mini
         samples/order_loop_mix.mini)
fi

"$BIN" --stats --search "${FILES[0]}" | tr -d '\r' | grep '^cost weights' || true
echo
echo "| program | unopt | fixed | searched | searched vs fixed | candidates | search ms |"
echo "|---|---:|---:|---:|---:|---:|---:|"

fail=0
for f in "${FILES[@]}"; do
  out=$("$BIN" --stats --search "$f" | tr -d '\r')
  col() { echo "$out" | awk -v a="$1" -v c="$2" '$1=="STAT" && $2==a {print $c}'; }
  unopt=$(col unoptimized 3)
  fixed=$(col fixed 3)
  searched=$(col searched 3)
  cand=$(col search_candidates 3)
  ms=$(col search_ms 3)
  if echo "$out" | grep -q MISMATCH; then
    echo "MISMATCH: optimized program differs from original: $f" >&2
    fail=1
  fi
  pct=$(awk -v f="$fixed" -v s="$searched" \
        'BEGIN { if (f > 0) printf "%.1f%%", (f - s) * 100.0 / f; else print "n/a" }')
  printf "| %s | %s | %s | %s | %s | %s | %s |\n" \
         "$(basename "$f")" "$unopt" "$fixed" "$searched" "$pct" "$cand" "$ms"
done
exit $fail
```

Run `chmod +x bench/run_bench.sh`.

- [ ] **Step 2: Write the C equivalents (inputs from `argv` so GCC cannot constant-evaluate them)**

`bench/c/loop_invariant.c`:

```c
/* Mirror of samples/loop_invariant.mini. a, b, n come from argv so the
 * optimizer cannot evaluate the whole program at compile time. */
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
```

`bench/c/loop_invariant_chain.c`:

```c
/* Mirror of samples/loop_invariant_chain.mini. */
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
```

`bench/c/loop_no_invariant.c`:

```c
/* Mirror of samples/loop_no_invariant.mini. */
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
```

`bench/c/order_strength_cse.c`:

```c
/* Mirror of samples/order_strength_cse.mini. */
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
```

`bench/c/order_loop_mix.c`:

```c
/* Mirror of samples/order_loop_mix.mini (k is the loop-invariant input). */
#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline)) int kernel(int k, int b, int n) {
    int i = 0, s = 0, c, r;
    (void)b;
    while (i < n) {
        c = k;
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
```

- [ ] **Step 3: Write `bench/gcc_compare.sh`**

```bash
#!/usr/bin/env bash
# OPTIONAL reference table. Compares the static instruction count of minicc's
# searched output against the static instruction count of the equivalent C
# function compiled by gcc -O0 / -O1. NOT a like-for-like comparison: see
# the caveats printed below and in REPORT.md.
set -euo pipefail
BIN=${BIN:-./minicc}
if ! command -v gcc >/dev/null 2>&1; then
  echo "gcc not found on PATH; skipping GCC side table" >&2
  exit 0
fi

# gcc output asm for `kernel`: count instruction lines (tab + lowercase letter),
# excluding directives (tab + '.') and labels.
count_insns() {
  gcc "$2" -S -o - "$1" | tr -d '\r' | awk '
    /^kernel:/ { f = 1; next }
    f && /\.seh_endproc|\.size[ \t]+kernel/ { f = 0 }
    f && /^\t[a-z]/ { n++ }
    END { print n + 0 }'
}

echo "| program | minicc searched (static ops) | gcc -O0 insns | gcc -O1 insns |"
echo "|---|---:|---:|---:|"
for c in bench/c/*.c; do
  name=$(basename "$c" .c)
  mini="samples/$name.mini"
  [ -f "$mini" ] || continue
  searched=$("$BIN" --stats --search "$mini" | tr -d '\r' \
             | awk '$1=="STAT" && $2=="searched" {print $4}')
  [ -z "$searched" ] && searched=$("$BIN" --stats "$mini" | tr -d '\r' \
             | awk '$1=="STAT" && $2=="fixed" {print $4}')
  printf "| %s | %s | %s | %s |\n" "$name" "$searched" \
         "$(count_insns "$c" -O0)" "$(count_insns "$c" -O1)"
done

cat <<'EOF'

Caveats (indicative only):
- Units differ: TAC operations (no registers, no addressing modes) vs. x86-64
  instructions, whole program vs. the `kernel` function. Initial assignments
  that the C version receives as parameters are counted in the TAC.
- The C versions take their inputs from argv so GCC cannot evaluate the closed
  program at compile time; the mini programs use literal inputs.
- GCC at -O1 and above is expected to win. This table positions the project;
  it does not claim superiority.
EOF
```

Run `chmod +x bench/gcc_compare.sh`.

- [ ] **Step 4: Run the benchmarks**

Run:

```bash
make && bench/run_bench.sh && bench/gcc_compare.sh
```

Expected: a Markdown table with one row per sample; `searched` is never greater than `fixed`; the three `order_*` rows show a strictly positive improvement; exit status 0 (no MISMATCH). The GCC table prints one row per `bench/c/*.c` plus the caveats. Paste both tables into `REPORT.md` in Task 15.

- [ ] **Step 5: Commit**

```bash
git add bench/
git commit -m "feat: benchmark table script and optional GCC side table

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 15: Documentation and final verification

**Files:**
- Modify: `README.md`, `REPORT.md`

**Interfaces:**
- Consumes: the tables produced in Tasks 13 and 14, the fuzz summary from Task 12.
- Produces: README flag documentation; a REPORT section stating exactly what is claimed, with measured numbers.

- [ ] **Step 1: Update `README.md`**

Make these edits:

1. In the "Run" section, after the `./minicc samples/valid.mini` example, add the flag table:

```
| Flag | Effect |
|---|---|
| `--search` | Search pass orderings; print the cheapest sequence and its TAC |
| `--budget N` | Cap the number of candidate evaluations (default 2000) |
| `--stats` | Print weighted dynamic cost / static op count and an equivalence check |
| `--explain` | Per-pass cost deltas and instruction diffs for the chosen sequence |
| `--learn` | Search, then record the result in `.minicc_cache` (`--cache PATH` to change) |
| `--use-cache` | Apply the cached/nearest sequence without searching |
| `--time` | Per-stage compile time (ms) |
| `--fuzz N [--seed S]` | Differential fuzzing instead of compiling a file |
| `--gen N [--seed S]` | Print a generated program of N top-level statements |
| `--loo a.mini b.mini ...` | Leave-one-out evaluation of the heuristic cache |
```

2. Add `make test` under "Build".
3. In the sample table add the three `order_*.mini` rows, described as "ordering-sensitive: a pass only pays off after another pass; fixed fold -> LICM order misses it".
4. In the module table add rows for `tacutil`, `strmap`, `cost`, `interp`, `passes`, `search`, `heuristic`, `explain`, `fuzz`, `pipeline`, `timing.h`.
5. In "Notes on scope", replace the sentence about LICM with: "LICM hoists only compiler temporaries (`t<N>`) and never a division by a non-literal; assignments to user variables stay in the loop because hoisting them is unsound for loops that run zero times."

- [ ] **Step 2: Add the results section to `REPORT.md`**

Append a new section `## 6. Search-based, self-verifying optimizer` containing:

- the pipeline description (cost model, interpreter oracle, passes, search, cache, explain, fuzzer);
- the cost-weight line (`./minicc --stats <file>` prints it);
- the `bench/run_bench.sh` table, pasted verbatim;
- the leave-one-out output (`./minicc --loo samples/order_*.mini samples/loop_*.mini samples/valid.mini samples/constant_folding.mini`), pasted verbatim, with the hit rate and "search gain recovered by cache" figure stated as measured;
- the `bench/compile_time.sh` tables before and after the LICM fix (from `/tmp/licm_before.txt` and `/tmp/licm_after.txt`);
- the `./minicc --fuzz 500 --seed 42` summary line;
- the optional GCC table with its caveats copied from `bench/gcc_compare.sh`;
- a "What this does and does not claim" list taken from spec §2, and a "Defects found and fixed by the oracle" list: fold changed float kind/precision (Task 4), LICM hoisted user variables and divisions (Task 5), temp-name collision guard (Task 9);
- a one-line statement that the Layer 4 window superoptimizer was not implemented.

- [ ] **Step 3: Full verification**

Run each command and confirm the stated result before claiming completion:

```bash
make clean && make && make test                      # ALL TESTS PASSED, no warnings
./minicc samples/valid.mini > /dev/null && echo OK   # default behaviour still works
bench/run_bench.sh > /dev/null && echo BENCH_OK      # no MISMATCH
./minicc --fuzz 500 --seed 42 | tail -1              # ... 0 failures
for f in samples/error_*.mini; do ./minicc "$f" | grep -c Errors; done   # error samples still report errors
```

Expected: every command succeeds; `make` prints no `warning:` lines; the three `error_*.mini` runs each print `1`.

- [ ] **Step 4: Commit**

```bash
git add README.md REPORT.md
git commit -m "docs: document search optimizer, flags, benchmarks and results

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Appendix: Stretch goal (not part of this plan's acceptance)

Layer 4, the window superoptimizer (spec §11), is deliberately not planned task-by-task. Start it only after Task 15 verification passes, and give it its own spec-to-plan pass: it needs a decision on the window language and the sampled-environment count, and its equivalence evidence is sampled, not proven, so the report must say so and the fuzzer must run with the new pass enabled.

---

## Self-Review

**1. Spec coverage**

| Spec section | Task |
|---|---|
| §5 cost model | 2 |
| §6 interpreter, equivalence, step limit, trap handling | 3 (and Review Focus 1, Tasks 8, 12) |
| §7 passes `copyprop`, `cse`, `strength`, `dce`; registry | 6, 7 |
| §8 search, memoization, budget | 8 |
| §9.1 heuristic cache, leave-one-out | 11 |
| §9.2 `--explain` | 10 |
| §9.3 fuzzing, broken-pass check | 12 |
| §10 `--time`, profiling, LICM fix | 9 (`--time`), 13 |
| §11 stretch | Appendix |
| §12 GCC side table with caveats | 14 |
| §13 CLI | 9, 10, 11, 12 |
| §14 ordering-sensitive benchmarks, tests | 9 and every task's tests |
| §16 risks | Review Focus; README/REPORT in 15 |

Gaps noted and handled: the spec's claim that existing fold and LICM are "unchanged" is superseded by Tasks 4 and 5 (documented in "Deviations").

**2. Placeholder scan:** no TBD/TODO; every code step has code. The `TASKnn-ANCHOR` comments in `main.c` are real source markers replaced in later tasks, not plan placeholders.

**3. Type consistency:** `SearchResult`/`SearchConfig` fields are used with the same names in `main.c`, `fuzz.c`, `heuristic.c`, tests. `interp_dynamic_cost` returns `long` (-1 on failure) everywhere. `PASS_REGISTRY[...].fn` signatures match `PassFn`. `Features`/`CacheEntry` fields match between `heuristic.h`, `heuristic.c`, tests. `SEARCH_MAX_SEQ` is defined in `search.h` and used by `heuristic.h`.

**4. Review Focus:** items 1 (Tasks 8, 12), 2 (Task 5), 3 (Task 4), 4 (Tasks 3, 6, 7, 8, 11), 5 (Tasks 9, 11) each have a named test.
