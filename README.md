# AI-Assisted Syntax Error Detection and Correction Compiler

A small compiler pipeline in C for a C-like mini language: lexing → parsing
(AST) → semantic analysis → three-address code generation → constant folding
→ loop-invariant code motion, plus a rule-based syntax-error suggestion
engine.

## Language

- Types: `int`, `float`
- Statements: variable declaration, assignment, `if`/`else`, `while`, `{ }` blocks
- Expressions: `+ - * / = == < > <= >=`, parentheses, identifiers, numeric literals

```
int x;
x = 5;
while (x > 0) {
  x = x - 1;
}
```

## Build

Requires a C11 compiler (`gcc`).

```
make
```

Produces the `minicc` executable. `make test` builds and runs the unit,
property and fuzz tests; `make clean` removes build output.

## Run

```
./minicc samples/valid.mini
```

For every source file, the driver prints, in order:
1. Tokens
2. AST (indented)
3. Symbol table
4. TAC (unoptimized)
5. TAC (optimized — constant folding applied)
6. TAC (further optimized — loop-invariant code motion applied on top)
7. Errors, each with a suggestion from the error engine

With no flags the output is exactly the staged listing above. Flags add
sections after the optimized TAC:

| Flag | Effect |
|---|---|
| `--search` | Search pass orderings; print the cheapest sequence and its TAC |
| `--budget N` | Cap the number of candidate evaluations (default 2000) |
| `--stats` | Weighted dynamic cost / static op count per stage, plus an equivalence check |
| `--explain` | Per-pass cost deltas and instruction diffs for the chosen sequence |
| `--learn` | Search, then record the result in `.minicc_cache` (`--cache PATH` to change) |
| `--use-cache` | Apply the cached/nearest sequence without searching (never worse than the fixed pipeline) |
| `--time` | Per-stage compile time (ms) |
| `--fuzz N [--seed S]` | Differential fuzzing of every pass instead of compiling a file |
| `--gen N [--seed S]` | Print a generated program of N top-level statements |
| `--loo a.mini b.mini ...` | Leave-one-out evaluation of the heuristic cache |

Benchmarks: `bench/run_bench.sh` (cost table), `bench/compile_time.sh`
(per-stage compile time on generated programs), `bench/gcc_compare.sh`
(optional, indicative GCC side table).

## Sample programs (`samples/`)

| File | Purpose |
|---|---|
| `valid.mini` | Fully valid program: declarations, `while`, `if/else`, mixed `int`/`float` |
| `error_missing_semicolon.mini` | Missing `;` — exercises panic-mode parse recovery |
| `error_undeclared_var.mini` | Use of an undeclared variable — semantic error |
| `error_unclosed_paren.mini` | Unclosed `(` in an `if` condition — parse error |
| `constant_folding.mini` | Constant expressions (`2 + 3 * 4`, `1 < 2`, ...) — shows unoptimized vs. optimized TAC side by side |
| `loop_invariant.mini` | `t = a * b` inside a `while` loop, with `a`/`b` never reassigned — the multiply (`t2 = a * b`) is hoisted into a preheader; the copy `t = t2` stays in the loop (user-variable assignments are never hoisted) |
| `loop_invariant_chain.mini` | Two chained invariant computations (`p = a * b; q = p + c;`) — the temps feeding them are hoisted in order |
| `loop_no_invariant.mini` | A loop where every assignment target changes every iteration — nothing is hoistable, TAC is unchanged by LICM |
| `loop_with_if.mini` | A loop body containing an `if`/`else` — LICM intentionally leaves it untouched (see "Notes on scope") |
| `order_copyprop_fold.mini` | Ordering-sensitive: copy propagation exposes a constant fold; the fixed fold → LICM order finds nothing |
| `order_strength_cse.mini` | Ordering-sensitive: strength reduction turns `a * 2` into `a + a`, which CSE then reuses |
| `order_loop_mix.mini` | Ordering-sensitive: copy propagation exposes an invariant multiply, strength reduction cheapens it, LICM hoists it |

## Module layout

| File | Responsibility |
|---|---|
| `lexer.c/h` | Tokenizer: keywords, identifiers, numbers, operators, punctuation |
| `parser.c/h` | Recursive-descent parser → AST, with panic-mode error recovery |
| `semantic.c/h` | Symbol table + redeclaration / undeclared-use / type-mismatch checks |
| `codegen.c/h` | AST → three-address code (TAC) |
| `optimizer.c/h` | Constant folding + loop-invariant code motion over TAC |
| `passes.c/h` | Copy propagation, strength reduction, CSE, DCE; pass registry; sequence application |
| `cost.c/h` | Weighted cost model for TAC instructions |
| `interp.c/h` | TAC interpreter: dynamic cost metric and correctness oracle |
| `search.c/h` | Pass-ordering search (BFS, memoized, budgeted) |
| `heuristic.c/h` | Feature-keyed cache of searched sequences, nearest-neighbour lookup, leave-one-out |
| `explain.c/h` | Per-pass cost deltas and instruction diffs |
| `fuzz.c/h` | Program generator and differential fuzzer |
| `pipeline.c/h` | Quiet front end used by tests, fuzzer and cache tooling |
| `tacutil.c/h`, `strmap.c/h`, `timing.h` | Shared TAC helpers, string map, wall-clock timer |
| `error_engine.c/h` | Rule-based `(expected, found) → suggestion` messages |
| `main.c` | Driver: runs the full pipeline, prints each stage, frees all memory |

See `REPORT.md` for how each module maps to the course syllabus.

## Notes on scope

- **Optimization** is constant folding, loop-invariant code motion
  (LICM), copy propagation, strength reduction, common subexpression
  elimination and dead code elimination, all basic-block scoped. The
  fixed pipeline runs fold then LICM; `--search` chooses a pass order per
  program. A variable assigned but never read afterward (e.g. `dead` in
  `constant_folding.mini`) is only removed when it is overwritten in the
  same block, because final variable values are observable.
- **LICM hoists only compiler temporaries** (`t<N>`), and never a
  division by a non-literal or zero divisor. Assignments to user
  variables stay in the loop: hoisting them is unsound for loops that run
  zero times or read the variable before assigning it.
- **A user variable named like a compiler temp** (`t1`, `t2`, ...)
  disables optimization for that program, with a warning.
- **LICM only hoists from straight-line loop bodies.** A `while` loop
  whose body contains an `if`/nested `while` (any control flow at all)
  is left completely unchanged — see `loop_with_if.mini`. Deciding
  whether a conditionally-executed assignment is still safe to run
  unconditionally before the loop needs real basic-block/dominator
  analysis, which is out of scope here; skipping the whole loop is the
  safe fallback rather than risk hoisting something incorrectly.
- **LLM error-suggestion fallback is stubbed**, not wired to a live API.
  `error_engine_try_llm_suggestion()` in `error_engine.c` always returns
  `NULL`, which the driver already treats as "no suggestion available —
  fall back to the rule table." The project runs correctly with **zero
  API key configured**, and there is no live network call anywhere in
  this build.
- All heap allocations (tokens, AST nodes, symbol table entries, TAC
  instructions, error lists) are freed in `main.c`'s cleanup pass at the
  end of `main()`.
