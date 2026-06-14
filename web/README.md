# Mocha 1995 — Playground & Engine Debugger

A modern web playground for Brendan Eich's original 1995 JavaScript engine. It
runs the **actual C engine** (compiled to WebAssembly) in your browser and
visualizes its internals: the scanner's token stream, the compiled bytecode, and
a full step-by-step execution trace.

Stack: **Vite + React + TypeScript + Tailwind CSS v4**, with the engine exposed
through a tiny instrumentation layer (`src/mo_web.c`, `src/mo_introspect.h`).

## What you get

- **Source editor** (CodeMirror) with the current debug line highlighted.
- **Console** with `print()` output, the final value, and rich error reporting
  (line, column, caret).
- **Tokens** — the scanner output (`mo_scan.c`), colour-coded by class.
- **Bytecode** — disassembled instructions per script/function (`mocha.def`),
  with operands and jump targets; the active instruction is highlighted as you
  step.
- **Interactive debugger** — play / pause / step / step-over / scrub through
  every interpreter instruction. The editor, bytecode, value stack, call stack
  and charts all stay in sync with the current step.
- **Value stack & call stack** — the real operand stack and call frames
  (with argument values) at each step.
- **Charts** — the engine pipeline (source → tokens → bytecode → interpreter),
  an execution timeline (stack size & call depth over time), and an opcode
  frequency profile.

## How it works

`src/mo_web.c` (in the repo root) embeds the engine and exposes a single
exported function, `mocha_run_json(source)`, which compiles and runs a program
and returns one JSON document describing the tokens, the disassembled bytecode of
every executed script, a per-instruction execution trace (program counter, value
stack, and call frame), the program output, and any error.

The trace is captured through `mocha_TraceHook` — a one-line hook added to the
interpreter loop in `src/mocha.c` that fires once per bytecode instruction. The
engine is otherwise unmodified.

## Develop

```sh
# 1. Build the engine to WebAssembly (requires Emscripten on PATH):
#    source /path/to/emsdk/emsdk_env.sh
npm run build:wasm        # -> public/engine/mocha.{mjs,wasm}

# 2. Install deps and start the dev server:
npm install
npm run dev               # http://localhost:5173

# Production build:
npm run build && npm run preview
```

The `public/engine/*` artifacts are git-ignored; regenerate them with
`build:wasm` (or `../build_wasm.sh`).
