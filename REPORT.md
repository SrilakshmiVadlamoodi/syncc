# Project Report — AI-Assisted Syntax Error Detection and Correction Compiler

**Course:** BCSE307P — Compiler Design Lab (30M project, team of 2)

## 1. Pipeline overview

The project implements a full mini-compiler pipeline for a small C-like
language (`int`/`float` types; declarations, assignment, `if`/`else`,
`while`, blocks; arithmetic and comparison expressions). Each stage is
isolated in its own module and communicates with the next through a
plain C data structure (token list → AST → symbol table → TAC list),
so each syllabus module has a corresponding, independently testable
source file.

## 2. Module-to-syllabus mapping

| Stage | Files | Syllabus Module | What it demonstrates |
|---|---|---|---|
| 1. Lexical analysis | `lexer.c/h` | Module 1 — Lexical Analysis | Hand-written scanner (no regex): character-by-character DFA-style recognition of keywords, identifiers, integer/float literals, operators, and punctuation; whitespace and `//`/`/* */` comment skipping; line/column tracking for diagnostics; invalid characters reported without aborting the scan. |
| 2. Syntax analysis | `parser.c/h` | Module 2 — Syntax Analysis | Recursive-descent parser, one function per grammar rule (`statement`, `var_decl`, `assign`, `if_stmt`, `while_stmt`, `block`, and a precedence chain `comparison → additive → multiplicative → primary` for expressions). Builds an AST as a tagged union (`NodeType` + `union`). Implements **panic-mode error recovery**: on an unexpected token, the error is recorded and the parser skips forward to the next `;` or `}` rather than aborting, so multiple syntax errors can be collected in a single run. |
| 3. Semantic analysis | `semantic.c/h` | Module 3 — Semantic Analysis | Builds a symbol table (name → type, declared line) while walking the AST. Checks: redeclaration, use-before-declaration, and type mismatch (narrowing a `float` expression into an `int` variable is flagged; `int → float` widening is allowed, matching ordinary C promotion). Errors are collected in a list rather than raised, consistent with the parser's non-aborting design. |
| 4. Intermediate code generation | `codegen.c/h` | Module 4 — Intermediate Code Generation | Walks the (semantically checked) AST and emits three-address code: `result = arg1 op arg2` for expressions, fresh `t<N>` temporaries per sub-expression, and `L<N>` labels with `goto`/`ifFalse ... goto` for `if`/`while` control flow — the standard TAC lowering pattern. |
| 5. Optimization | `optimizer.c/h` | Module 5 — Optimization | Two passes, applied in sequence. **Constant folding**: any `TAC_BINOP` whose both operands are literal text is evaluated at compile time and rewritten to a plain assignment (e.g. `t1 = 2 + 3` → `t1 = 5`), covering both arithmetic and comparison operators. **Loop-invariant code motion (LICM)**: `while` loops are located by their back-edge (a `TAC_GOTO` jumping to an earlier `TAC_LABEL`); for a loop whose body is a single straight-line block, any assignment whose operands are all loop-invariant and whose target is assigned exactly once in the body is hoisted into a preheader placed just before the loop. The driver prints unoptimized TAC, TAC after folding, and TAC after folding + LICM, all three side by side. |
| 6. Error suggestion engine | `error_engine.c/h` | Cross-cutting / AI hook | A static rule table keyed on `(expected_token, found_token)` maps common mistakes (missing semicolon, unclosed parenthesis, reserved word used as an identifier, etc.) to a human-readable suggestion, with a generic `"Syntax error near line X"` fallback when nothing matches. An optional LLM (Claude Haiku) fallback hook exists in the same interface but is currently a stub — see §4. |
| 7. Driver / CLI | `main.c`, `Makefile` | — | Reads a `.mini` file, runs all six stages in order, prints tokens / AST / symbol table / TAC (before and after optimization) / errors with suggestions, and frees every heap allocation before exit. |

Both team members should be able to walk through this table end-to-end
in the viva; the front end (lexer/parser/semantic) and back end
(codegen/optimizer/error engine) were split across the two authors per
the original module split, with the driver as shared integration work.

## 3. C-specific design decisions

The original spec was written with a Python implementation in mind
(dicts/classes, dynamic lists, exceptions). Porting to C required a few
deliberate adjustments, made explicit here since they're not visible
from a syllabus-module mapping alone:

- **AST representation**: a single `NodeType` enum plus a tagged
  `union` inside `AstNode` (`parser.h`), rather than one struct per
  node type. Keeps the AST a single type, simplifies the tree-walking
  functions (`ast_print`, `ast_free`, `analyze_stmt`, `gen_stmt`) to one
  `switch` each instead of a class hierarchy.
- **Collections**: every list in the pipeline (`TokenList`, `AstList`,
  `SymbolTable`, TAC's `TACList`, and both error lists) is the same
  realloc-based growable array pattern — no fixed-size caps, so larger
  test programs aren't at risk of silent truncation.
- **Strings and memory ownership**: the lexer manually copies lexemes
  with pointer arithmetic (no regex, no `strdup` for portability across
  compilers). Ownership follows one rule throughout: a `Token` returned
  by the parser's `expect()`/`advance_tok()` is *borrowed* (owned by the
  `TokenList`), while every string stored inside an `AstNode` or
  `TACInstr` is a fresh heap copy owned by that struct. This is what
  makes the single cleanup pass in `main()` correct without double-frees
  or leaks.
- **Error handling without exceptions**: `ParseError` and
  `SemanticError` are plain structs appended to growable lists, mirroring
  the spec's structured-error intent without C++-style exceptions.
  Parsing never aborts on a bad token; it recovers and keeps collecting
  errors (see §2, syntax analysis).
- **Optimization scope reduced to constant folding + loop-invariant code
  motion; dead code elimination is still out.** The original spec also
  asked for dead code elimination. That was dropped to keep the project
  weekend-sized given the extra boilerplate C requires relative to
  Python (manual TAC list management, explicit string ownership per
  instruction, no garbage collector); the `dead` variable in
  `constant_folding.mini` is a known, intentional gap — it is assigned
  but never used, and is *not* removed by this build. LICM was added on
  top of folding since it's the other optimization most directly tied to
  `while` loops (Module 5 + Module 4's control-flow lowering together),
  and — unlike full dead-code elimination — it stays tractable without a
  real CFG/dominator pass by restricting itself to straight-line loop
  bodies (`samples/loop_with_if.mini` shows the restriction: a loop body
  with an `if` inside it is left untouched rather than risk hoisting a
  conditionally-executed assignment incorrectly).
- **Memory cleanup**: `main.c` runs an explicit teardown pass at the end
  of `main()` (`taclist_free`, `symtab_free`, `ast_free`,
  `tokenlist_free`, etc.) for every structure it allocated, since this
  may be checked directly during grading.

## 4. AI hook (error suggestion engine)

The rule-based suggestion engine (`error_engine.c`) is always active and
requires no configuration. The interface also includes
`error_engine_try_llm_suggestion()`, matching the spec's optional
Claude-Haiku fallback (`expected`/`found`/offending source line only,
`max_tokens` ~60, silently disabled with no API key). **This call is
currently stubbed** — it unconditionally returns `NULL`, which the
driver already treats as "no LLM suggestion, use the rule table
instead." Wiring up the live HTTP call was scoped out of this pass; the
call site in `main.c` needs no changes to support it later.

## 5. Non-goals (per original spec)

- No code generation to a real target machine — TAC is the final output.
- No register allocation.
- No functions or loops beyond `while` and top-level statements.
- No GUI — terminal output only.

## 6. Search-based, self-verifying optimizer

### 6.1 What was added

| Piece | Files | Role |
|---|---|---|
| Cost model | `cost.c/h` | One weight table: assign=1, add/sub=1, compare=1, mul=3, div=10, ifFalse=2, goto=1, label=0 |
| TAC interpreter | `interp.c/h` | Executes TAC; returns weighted **dynamic cost** and final variable state. It is both the benchmark metric and the correctness oracle |
| Extra passes | `passes.c/h` | Copy propagation, strength reduction (`x*2 → x+x`, `x*1`, `x+0`, `x-0`, `x/1`), common subexpression elimination, dead code elimination; all basic-block scoped; `x*0` deliberately not rewritten |
| Pass-ordering search | `search.c/h` | Breadth-first over pass sequences (depth ≤ 4, budget 2000), memoized by TAC hash, scored by the interpreter |
| Heuristic cache | `heuristic.c/h` | Feature-keyed cache of searched sequences with nearest-neighbour lookup (a lookup table, not machine learning); evaluated leave-one-out |
| Attribution trace | `explain.c/h` | `--explain`: per-pass cost delta and instruction diff |
| Differential fuzzer | `fuzz.c/h` | Seeded generator of valid terminating programs; checks every pass, the fixed pipeline, all passes twice, and the searched sequence against the unoptimized program under random initial environments |

### 6.2 Defects found and fixed by the oracle

The interpreter oracle exposed three problems in the pre-existing passes, each fixed with a test that failed first:

1. **Constant folding changed value kind and precision.** `2.0 * 2.0` folded to the *integer* `4`; `0.1 + 0.2` lost digits (`%g`); integer overflow was folded with undefined behaviour. Folding now keeps float literals as floats, round-trips exactly, and declines to fold overflow or results needing exponent notation.
2. **LICM hoisted user-variable assignments and divisions.** That is unsound for a loop that runs zero times, or that reads the variable before assigning it, and a hoisted division can introduce a divide-by-zero. LICM now hoists only compiler temporaries, and divisions only by a nonzero literal. The cost is that `t = t2` stays in the loop in `loop_invariant.mini` (the multiply itself is still hoisted).
3. **A user variable named like a compiler temp** (`t1`) is indistinguishable from codegen temps by spelling. Optimization is disabled for such programs, with a warning.

### 6.3 Results

All numbers below are produced by the scripts named; weights are the table in 6.1.

**Weighted dynamic cost** (`bench/run_bench.sh`):

| program | unopt | fixed | searched | searched vs fixed | candidates | search ms |
|---|---:|---:|---:|---:|---:|---:|
| valid.mini | 52 | 52 | 52 | 0.0% | 0 | 0.130 |
| constant_folding.mini | 23 | 21 | 9 | 57.1% | 6 | 0.314 |
| loop_invariant.mini | 127 | 100 | 98 | 2.0% | 5 | 0.568 |
| loop_invariant_chain.mini | 120 | 99 | 90 | 9.1% | 12 | 0.928 |
| loop_no_invariant.mini | 45 | 45 | 45 | 0.0% | 0 | 0.121 |
| loop_with_if.mini | 74 | 74 | 74 | 0.0% | 0 | 0.164 |
| order_copyprop_fold.mini | 6 | 6 | 3 | 50.0% | 6 | 0.133 |
| order_strength_cse.mini | 108 | 108 | 90 | 16.7% | 7 | 0.844 |
| order_loop_mix.mini | 137 | 137 | 108 | 21.2% | 8 | 0.423 |

"fixed" is the original fold → LICM pipeline. The three `order_*` programs were written to be **ordering-sensitive** (a pass only pays off after another); they are not a neutral sample of real code, and the gains on the other six rows are the more representative figure (0% to 57%).

**Heuristic cache, leave-one-out** (`./minicc --loo samples/order_*.mini samples/loop_*.mini samples/valid.mini samples/constant_folding.mini`):

```
program                              lookup        unopt    fixed   cached searched
samples/order_copyprop_fold.mini     near(4)           6        6        6        3
samples/order_loop_mix.mini          exact           137      137      117      108
samples/order_strength_cse.mini      exact           108      108       96       90
samples/loop_invariant.mini          exact           127      100      100       98
samples/loop_invariant_chain.mini    exact           120       99       92       90
samples/loop_no_invariant.mini       near(2)          45       45       45       45
samples/loop_with_if.mini            exact            74       74       74       74
samples/valid.mini                   exact            52       52       52       52
samples/constant_folding.mini        near(4)          23       21       21        9
hit rate: 9/9   search gain recovered by cache (programs where search beats fixed): 36%
```

Each row's cache is trained only on the *other* eight programs. The cache recovers 36% of the search's gain on average over the programs where search beats the fixed pipeline; it is coarse (features are quantized to six buckets, so different programs often share a key). The cache never does worse than the fixed pipeline: when a cached/nearest sequence costs more, the compiler falls back (this was a defect found by the leave-one-out run: `constant_folding.mini` first came out at 23 vs 21). With nine programs this is a small-sample result.

**Compile time** (`bench/compile_time.sh`, generated programs of N top-level statements, ms):

LICM stage, before and after the linear-time rewrite:

| N | LICM before | LICM after |
|---:|---:|---:|
| 1000 | 5.3 | 2.8 |
| 2000 | 16.9 | 3.2 |
| 4000 | 82.3 | 6.3 |
| 8000 | 322.1 | 14.2 |

Before: roughly ×4 per doubling (quadratic). After: roughly ×2 (linear). Search (budget 300) at N = 1000 / 2000 / 4000 took 7.8 / 19.9 / 57.0 s before the LICM fix and 6.7 / 8.7 / 18.5 s after. **Search is the dominant compile-time cost on large programs**; on the small sample programs it takes under a millisecond. The cache lookup and the fixed pipeline do not pay this cost, which is the point of the cache.

**Correctness** (`./minicc --fuzz N --seed S`): 500 programs (seed 42), 18,000 pass-vs-original comparisons, 0 failures; 400 programs each for seeds 1, 42 and 777 earlier, 43,200 comparisons, 0 failures. The fuzzer is itself tested: a deliberately broken pass (`+` rewritten to `-`) is caught.

### 6.4 Optional GCC reference table (`bench/gcc_compare.sh`)

| program | minicc searched (static ops) | gcc -O0 insns | gcc -O1 insns |
|---|---:|---:|---:|
| loop_invariant | 13 | 22 | 12 |
| loop_invariant_chain | 16 | 27 | 12 |
| loop_no_invariant | 9 | 19 | 12 |
| order_loop_mix | 14 | 24 | 11 |
| order_strength_cse | 16 | 31 | 13 |

Indicative only. The units differ (TAC operations with no registers or addressing modes vs. x86-64 instructions of the `kernel` function), the C versions take their inputs from `argv` so GCC cannot evaluate the closed program at compile time, and the TAC count includes initial assignments the C version receives as parameters. GCC at `-O1` is expected to win and mostly does; the table positions the project and does not claim superiority.

### 6.5 What this does and does not claim

- Claims: the searched order lowers weighted dynamic cost relative to the fixed pipeline on the benchmark set (numbers above); every optimized program matches the original on all user variables (interpreter on the benchmarks, fuzzer on generated programs); the compile-time cost of search is measured and the cache avoids it at a measured loss of quality; one hot spot (LICM) was found by profiling and fixed.
- Does not claim: general superiority over GCC/LLVM; that the cost model reflects real hardware; that the cache is machine learning; any figure not produced by the scripts above.
- Not implemented: the stretch-goal window superoptimizer (spec §11).
