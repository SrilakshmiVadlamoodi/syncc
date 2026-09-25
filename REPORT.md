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
