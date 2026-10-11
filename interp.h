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
