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

Produces the `minicc` executable. `make clean` removes build output.

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

## Sample programs (`samples/`)

| File | Purpose |
|---|---|
| `valid.mini` | Fully valid program: declarations, `while`, `if/else`, mixed `int`/`float` |
| `error_missing_semicolon.mini` | Missing `;` — exercises panic-mode parse recovery |
| `error_undeclared_var.mini` | Use of an undeclared variable — semantic error |
| `error_unclosed_paren.mini` | Unclosed `(` in an `if` condition — parse error |
| `constant_folding.mini` | Constant expressions (`2 + 3 * 4`, `1 < 2`, ...) — shows unoptimized vs. optimized TAC side by side |
| `loop_invariant.mini` | `t = a * b` inside a `while` loop, with `a`/`b` never reassigned — hoisted into a preheader above the loop |
| `loop_invariant_chain.mini` | Two chained invariant computations (`p = a * b; q = p + c;`) — both hoisted, in order, since the second depends on the first |
| `loop_no_invariant.mini` | A loop where every assignment target changes every iteration — nothing is hoistable, TAC is unchanged by LICM |
| `loop_with_if.mini` | A loop body containing an `if`/`else` — LICM intentionally leaves it untouched (see "Notes on scope") |

## Module layout

| File | Responsibility |
|---|---|
| `lexer.c/h` | Tokenizer: keywords, identifiers, numbers, operators, punctuation |
| `parser.c/h` | Recursive-descent parser → AST, with panic-mode error recovery |
| `semantic.c/h` | Symbol table + redeclaration / undeclared-use / type-mismatch checks |
| `codegen.c/h` | AST → three-address code (TAC) |
| `optimizer.c/h` | Constant folding + loop-invariant code motion over TAC |
| `error_engine.c/h` | Rule-based `(expected, found) → suggestion` messages |
| `main.c` | Driver: runs the full pipeline, prints each stage, frees all memory |

See `REPORT.md` for how each module maps to the course syllabus.

## Notes on scope

- **Optimization** is constant folding plus loop-invariant code motion
  (LICM). Dead code elimination was intentionally dropped to keep the
  project weekend-sized in C (see `REPORT.md` for rationale) — a
  variable assigned but never read afterward (e.g. `dead` in
  `constant_folding.mini`) is *not* removed.
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
