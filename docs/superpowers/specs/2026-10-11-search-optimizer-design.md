# Self-Verifying, Search-Based Optimizer — Design Spec

Date: 2026-10-11
Project: `minicc` (BCSE307P mini compiler, C)
Status: draft for review

## 1. Purpose

The course project now needs something unique. The goal is a **measurable,
defensible efficiency claim** for this compiler, covering both
generated-code speed and compile-time speed, without claiming to beat
production compilers in general.

The contribution is a self-verifying, search-based optimizer layered on the
existing TAC pipeline:

1. A **cost model + TAC interpreter** that scores code (weighted dynamic
   cost) and acts as a correctness oracle.
2. **Additional optimization passes** so there is something to order.
3. A **pass-ordering search** that picks the cheapest pass sequence per
   program.
4. A **feature-keyed heuristic cache** that reuses searched results so later
   compiles skip the search (Layer 1).
5. A **per-pass attribution trace** that explains where each saving came from
   (Layer 2).
6. **Differential fuzzing** that checks optimized programs against
   unoptimized ones over many generated programs (Layer 3).
7. An **optional GCC side table** as a clearly caveated reference point.

Stretch goal (Layer 4, only if everything above lands early): a window
superoptimizer, see section 11.

## 2. Claims this spec supports (and does not)

Supported, each backed by numbers the bench tooling prints:

- Searched pass order reduces weighted dynamic cost by N% vs. the fixed
  fold -> LICM pipeline on the benchmark set.
- Every optimized program has the same final user-variable state as the
  unoptimized program, checked by the interpreter on all benchmarks and on
  K generated programs by the fuzzer.
- The compile-time cost of search is quantified, and the heuristic cache
  recovers most of the searched benefit at near fixed-pipeline compile time,
  evaluated on held-out programs.
- Compile-time hot spots were identified by profiling and fixed, with
  before/after timings.
- (Optional table) indicative comparison against `gcc -O0/-O1`.

Not claimed:

- "More efficient than GCC/LLVM" in general.
- Any figure not produced by the bench output.
- That the cost model reflects real hardware. It is a defined, tweakable
  model.
- That the heuristic cache is machine learning. It is a feature-keyed
  cache of searched results with nearest-neighbour lookup.

Novelty framing: phase-ordering search and cost models are established
ideas. What is new at this project's level is the combination in a small,
readable compiler: interpreter as both metric and oracle, search distilled
into a reusable heuristic, per-pass explanation, and fuzz verification,
reported together with the compile-time trade-off.

## 3. Existing code this builds on

- `codegen.h`: `TACInstr {op, result, arg1, arg2, binop}`, `TACList`;
  operands are strings (variable, temp `t<N>`, or numeric literal). Ops:
  `TAC_ASSIGN`, `TAC_BINOP`, `TAC_LABEL`, `TAC_GOTO`, `TAC_IF_FALSE_GOTO`.
- `optimizer.c`: `optimize_constant_folding`, `optimize_loop_invariant_code_motion`.
  Every pass takes a `TACList *` and returns a **new** `TACList *`; the
  input is untouched; the caller owns the result.
- `main.c`: runs the pipeline and prints each stage.

All new passes keep that signature and ownership convention so they compose
and the search can hold several candidate lists at once.

## 4. Architecture and new modules

| File | Responsibility | Depends on |
|---|---|---|
| `cost.c/h` | Cost table: weight per TAC op / binop | `codegen.h` |
| `interp.c/h` | Execute a `TACList`; final env + dynamic cost + status | `cost.h` |
| `passes.c/h` | New passes + `PassInfo` registry (all passes incl. existing) | `codegen.h`, `optimizer.h` |
| `search.c/h` | Pass-sequence search, memoization, budget | `passes.h`, `interp.h` |
| `heuristic.c/h` | Feature extraction, cache file, nearest-neighbour lookup | `codegen.h`, `search.h` |
| `explain.c/h` | Per-step cost deltas and instruction diffs | `passes.h`, `interp.h` |
| `fuzz.c/h` | Seeded program generator + differential checker | whole pipeline |
| `main.c` | New CLI flags, timing | all |
| `bench/` | Bench driver script, extra benchmarks, optional C equivalents | `minicc` |

Each unit is usable and testable on its own: the interpreter runs any TAC,
a pass is `TACList* -> TACList*`, the search only needs the registry plus the
interpreter.

## 5. Cost model (`cost.c/h`)

A single `const CostModel` struct (one table), so it is easy to state and
tweak. Default weights, in abstract cost units:

| Operation | Weight |
|---|---|
| `TAC_ASSIGN` | 1 |
| `+`, `-` | 1 |
| comparison (`== < > <= >=`) | 1 |
| `*` | 3 |
| `/` | 10 |
| `TAC_IF_FALSE_GOTO` | 2 |
| `TAC_GOTO` | 1 |
| `TAC_LABEL` | 0 |

`int cost_of_instr(const TACInstr *)` returns the weight. The report must
print the weight table next to every result.

## 6. Interpreter (`interp.c/h`)

```c
typedef enum { INTERP_OK, INTERP_STEP_LIMIT, INTERP_DIV_ZERO } InterpStatus;

typedef struct Env Env;              /* opaque: name -> Value map (small array/hash);
                                        env_create/env_set/env_get/env_free in interp.h */
typedef struct {
    InterpStatus status;
    long   dynamic_cost;             /* sum of cost over executed instructions */
    long   steps;                    /* instructions executed */
    Env   *final_env;                /* owned by result */
} InterpResult;

InterpResult interp_run(const TACList *list, const Env *initial, long step_limit);
void interp_result_free(InterpResult *r);
```

Semantics, kept consistent with `fold_binop` in `optimizer.c`:

- Values are `{int long | float double}`. A literal containing `.` is float.
  A binary op is float if either side is float, else integer (truncating
  division). Comparisons yield integer 0/1.
- `ifFalse x goto L` jumps when `x` is zero.
- A variable read before it is written takes its value from `initial`, or
  integer 0 if absent.
- Division by zero stops execution with `INTERP_DIV_ZERO`.
- `step_limit` bounds execution (default 1,000,000). Hitting it returns
  `INTERP_STEP_LIMIT`, so a bad rewrite can never hang the compiler.

**Equivalence** of two programs under an initial env: both finish with
`INTERP_OK` and the final values of every **user variable** match. User
variables are the symbol-table names, not `t<N>` temps, because passes may
legitimately drop or renumber temps. If the original program ends with
`INTERP_DIV_ZERO` or `INTERP_STEP_LIMIT`, the case is skipped, not failed
(behaviour there is treated as undefined, which lets DCE remove a dead
division).

Known limitation, stated in the report: temps are recognised by their
`t<N>` spelling, so a user variable literally named `t1` would collide with
codegen temps. This pre-exists in `codegen.c`; the interpreter relies on the
symbol table rather than spelling to pick user variables.

## 7. Passes (`passes.c/h`)

```c
typedef TACList *(*PassFn)(TACList *input);
typedef struct { const char *name; PassFn fn; } PassInfo;

extern const PassInfo PASS_REGISTRY[];   /* NULL-name terminated */
size_t pass_count(void);
```

Registry entries, in order: `fold` (existing), `licm` (existing),
`copyprop`, `cse`, `strength`, `dce`.

All new passes are **basic-block scoped**: a block is a maximal run of
instructions with no `TAC_LABEL` inside and ending at a `TAC_GOTO` /
`TAC_IF_FALSE_GOTO`. This avoids needing a dominator framework, and each
pass is conservative where it cannot prove safety.

| Pass | Transformation (within a block) | Safety rule |
|---|---|---|
| `copyprop` | after `x = y` (y a name or literal), replace later reads of `x` with `y` | stop when `x` or `y` is reassigned |
| `cse` | reuse an earlier `a op b` result instead of recomputing | invalidate entry when `a`, `b`, or the holder is reassigned |
| `strength` | `x*2 -> x+x`, `x*1 -> x`, `x+0 -> x`, `x-0 -> x`, `x/1 -> x` | exact for int and float. `x*0` is **not** rewritten (wrong for float inf/NaN) |
| `dce` | (a) remove assignments to a temp never read anywhere, iterated to fixed point; (b) remove a store overwritten later in the same block with no read between | user variables' final values are observable, so only (b) touches them |

Existing `fold` and `licm` are unchanged.

Each pass must be idempotent-or-monotone: running it again never increases
dynamic cost (checked in tests).

## 8. Pass-ordering search (`search.c/h`)

```c
typedef struct {
    int    max_depth;       /* default 4 */
    int    budget;          /* max candidate evaluations, default 2000 */
} SearchConfig;

typedef struct {
    TACList *best;          /* owned */
    int      sequence[16];  /* indices into PASS_REGISTRY */
    int      sequence_len;
    long     best_cost;     /* dynamic cost under the interpreter */
    long     baseline_cost; /* cost of the fixed fold -> licm pipeline */
    int      candidates;    /* evaluations performed */
    double   seconds;
} SearchResult;

SearchResult search_best_sequence(TACList *input, const SearchConfig *cfg,
                                  const Env *initial);
```

Algorithm:

1. State = a `TACList`. Start from the input TAC.
2. Expand: apply each registry pass to the state, producing a successor.
3. Hash the successor's instruction text. Skip if already seen (memo).
4. Evaluate the successor with the interpreter and record its dynamic cost,
   tie-broken by static instruction count.
5. Continue breadth-first up to `max_depth` or until `budget` candidates
   have been evaluated or no new state appears (fixed point).
6. Return the cheapest state and its pass sequence. `baseline_cost` is the
   cost of the existing fold -> licm pipeline for comparison.

Passes are individually sound, so every explored state is semantically
equal to the input. The interpreter is used for scoring, and the
differential fuzzer (section 9.3) for checking.

## 9. Layers

### 9.1 Layer 1 — Feature-keyed heuristic cache (`heuristic.c/h`)

Goal: avoid paying the search cost on every compile.

- **Features** (cheap, from static TAC): instruction count, loop count
  (back-edges), mul count, div count, branch count, foldable-binop count
  (both operands literal), copy count (`x = y`). Each is quantized into
  small buckets.
- **Cache file** `.minicc_cache` (text): one line per entry,
  `<features>\t<pass,pass,...>\t<cost_ratio>`.
- `--learn`: run the search, then append/update the entry for the program.
- `--use-cache`: look up the feature vector. Exact match is used if
  present; otherwise the nearest neighbour (L1 distance) within a
  threshold. Apply the cached sequence directly with no search. On a miss,
  fall back to the fixed pipeline and note it in the output.
- Applying a cached sequence can never break correctness, since every pass
  is sound; only optimization quality can vary.

Evaluation (honest by construction): leave-one-out over the benchmark set.
For each program, train the cache on all other programs, then compile it
with `--use-cache`. Report hit rate, cost vs. full search, and cost vs. fixed
pipeline. This prevents the circular "trained on the test set" result.

### 9.2 Layer 2 — Per-pass attribution trace (`explain.c/h`)

`--explain` prints, for the chosen sequence, each step in order:

```
[cse]   cost 410 -> 392  (-18)   instrs 14 -> 12
   - t3 = a * b
   + t3 = t1
```

- Cost deltas come from the interpreter run before and after each pass.
- Instruction changes are a simple line diff (LCS over instruction text;
  programs are small).
- Aggregated at the end: total cost saved per pass across the sequence.

### 9.3 Layer 3 — Differential fuzzing (`fuzz.c/h`)

`minicc --fuzz N --seed S`:

1. Generate N programs from a seeded generator: declarations, assignments
   over random expressions, `if/else`, and `while` loops in a counter form
   with a bounded trip count (so loops terminate). Division uses nonzero
   literal divisors only.
2. Run each through lex -> parse -> semantic -> codegen (programs rejected by
   the front end are counted and skipped).
3. For each program, compare the unoptimized TAC against: each pass alone,
   the fixed pipeline, the searched sequence, and the cached sequence.
4. Compare under K random initial environments (default K = 4). The
   optimizer must be correct for any initial values of unassigned
   variables.
5. Report totals (programs, checks, skipped, failures). On a failure, print
   the seed, the program source, the pass name and both final states.

## 10. Compile-time speed

- `--time` prints wall-clock per stage (lex, parse, semantic, codegen, each
  pass, search).
- Profile on a generated **large** program (the fuzzer generator with a
  size parameter) because the sample programs compile in microseconds and
  would produce noise.
- Known suspect: `optimize_loop_invariant_code_motion` rescans the loop body
  per instruction per operand (`name_assign_count`), which is quadratic or
  worse. Fix by precomputing a per-loop assign-count table once. Other hot
  spots are decided by measurement, not guessed.
- Report fixed-pipeline time, search time, and cached-lookup time side by
  side with the cost achieved by each: the compile-time vs. code-quality
  trade-off.

## 11. Stretch goal — Layer 4: window superoptimizer

Only if everything above is complete and verified.

- Enumerate short TAC windows (2 to 3 instructions over a small variable set
  and constants), find the cheapest equivalent window proven by the
  interpreter over a set of input environments, and store as peephole
  rewrites.
- Verified rewrites are applied as an additional pass (`superopt`) in the
  registry.
- Risk: window equivalence by sampled environments is not a proof. The
  spec requires the report to say "equivalent on N sampled environments"
  and to run the fuzzer with the pass enabled.

## 12. Optional GCC side table (`bench/`)

- `bench/c/<name>.c`: hand-written C equivalents of 5 to 8 benchmarks.
- `bench/gcc_compare.sh`: for each, `gcc -O0 -S` and `gcc -O1 -S`, then
  count instructions in the hot loop body; print next to the TAC static
  operation count of the same loop in `minicc`'s optimized output.
- The script skips with a message if `gcc` is not on `PATH`.

Caveats that must appear next to the table in the report:

- Different units and abstraction levels (TAC operations vs. x86
  instructions, no register allocation or addressing modes in TAC).
  Indicative only.
- Closed programs are fully constant-evaluable, so `gcc -O1` may delete
  the whole computation. The C equivalents must take their inputs from
  `argv` or `volatile` variables, and say so.
- GCC at `-O1` and above is expected to win. The reported claim is
  positioning, not superiority.

## 13. CLI and driver changes

| Flag | Effect |
|---|---|
| (none) | Behaviour unchanged from today |
| `--search` | Run the pass-ordering search and use the best sequence |
| `--budget N` | Cap candidate evaluations for the search |
| `--learn` | Search and record the result in the cache |
| `--use-cache` | Apply the cached/nearest sequence, no search |
| `--explain` | Print the per-pass attribution trace |
| `--stats` | Print dynamic cost and weights table, before and after |
| `--time` | Per-stage timings |
| `--fuzz N --seed S` | Run the differential fuzzer instead of compiling a file |

`main.c` retains the existing staged output; new sections are added after
the optimized TAC, and `usage:` is updated.

## 14. Benchmarks and testing

**Ordering-sensitive benchmarks** (new, in `samples/`): programs where one
pass only pays off after another, for example copy propagation exposing a
fold, or strength reduction exposing a CSE. These are named and flagged as
ordering-sensitive in the report, alongside the existing samples, so the
set is not presented as a neutral sample of real code.

**Tests** (a `make test` target, plain C or shell, no framework):

- Interpreter: hand-computed final states for each existing sample,
  including a float case, a loop, and a div-by-zero trap.
- Each pass: output equivalent to input under the interpreter, cost not
  increased, expected rewrite on a targeted sample.
- Search: result cost <= fixed pipeline cost on every sample, and the
  returned sequence reproduces `best` when re-applied.
- Cache: lookup exact, lookup nearest, miss fallback, leave-one-out harness.
- Fuzzer: a fixed seed run with zero failures, plus a deliberately broken
  pass (test-only) that the fuzzer must catch.
- Memory: every `*_free` path exercised under the existing driver's
  cleanup, as `main.c` does today.

## 15. Out of scope

- Real machine-code backend and register allocation.
- Equality saturation / e-graphs.
- Dominator trees and a general control-flow-graph framework; passes stay
  basic-block scoped.
- Per-loop pass ordering (needs the loop-region abstraction above).
- Interprocedural or whole-program analysis (the language has no
  functions).

## 16. Risks

| Risk | Mitigation |
|---|---|
| Passes do not interact, so search gains ~0% | Ordering-sensitive benchmarks, reported honestly |
| Cache hit rate low on a small benchmark set | Nearest-neighbour lookup; report leave-one-out hit rate as-is |
| Float semantic corner cases in rewrites | `x*0` excluded; fuzzer runs with float variables |
| Search time blows up | Depth bound, memoization, `--budget` |
| Compile-time numbers noisy on tiny inputs | Profile on generated large programs, repeat runs |
| GCC table misread as a superiority claim | Mandatory caveats in section 12 |
