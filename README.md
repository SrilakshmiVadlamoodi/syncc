# AI-Assisted Syntax Error Detection and Correction Compiler

A small compiler pipeline in C for a C-like mini language: lexing → parsing
(AST) → semantic analysis → three-address code generation → constant-folding
optimization, plus a rule-based syntax-error suggestion engine.

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
6. Errors, each with a suggestion from the error engine

## Sample programs (`samples/`)

| File | Purpose |
|---|---|
| `valid.mini` | Fully valid program: declarations, `while`, `if/else`, mixed `int`/`float` |
| `error_missing_semicolon.mini` | Missing `;` — exercises panic-mode parse recovery |
| `error_undeclared_var.mini` | Use of an undeclared variable — semantic error |
| `error_unclosed_paren.mini` | Unclosed `(` in an `if` condition — parse error |
| `constant_folding.mini` | Constant expressions (`2 + 3 * 4`, `1 < 2`, ...) — shows unoptimized vs. optimized TAC side by side |

## Module layout

| File | Responsibility |
|---|---|
| `lexer.c/h` | Tokenizer: keywords, identifiers, numbers, operators, punctuation |
| `parser.c/h` | Recursive-descent parser → AST, with panic-mode error recovery |
| `semantic.c/h` | Symbol table + redeclaration / undeclared-use / type-mismatch checks |
| `codegen.c/h` | AST → three-address code (TAC) |
| `optimizer.c/h` | Constant folding over TAC |
| `error_engine.c/h` | Rule-based `(expected, found) → suggestion` messages |
| `main.c` | Driver: runs the full pipeline, prints each stage, frees all memory |

See `REPORT.md` for how each module maps to the course syllabus.

## Notes on scope

- **Optimization** is constant folding only. Dead code elimination was
  intentionally dropped to keep the project weekend-sized in C (see
  `REPORT.md` for rationale) — a variable assigned but never read
  afterward (e.g. `dead` in `constant_folding.mini`) is *not* removed.
- **LLM error-suggestion fallback is stubbed**, not wired to a live API.
  `error_engine_try_llm_suggestion()` in `error_engine.c` always returns
  `NULL`, which the driver already treats as "no suggestion available —
  fall back to the rule table." The project runs correctly with **zero
  API key configured**, and there is no live network call anywhere in
  this build.
- All heap allocations (tokens, AST nodes, symbol table entries, TAC
  instructions, error lists) are freed in `main.c`'s cleanup pass at the
  end of `main()`.
