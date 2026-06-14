/**
 * TypeScript mirror of the JSON document emitted by `mocha_run_json` in
 * src/mo_web.c. Keep these in sync with the C serializer.
 */

/** A single scanned token from the original Mocha scanner (mo_scan.c). */
export interface Token {
  index: number
  /** Token class, e.g. "FUNCTION", "NAME", "NUMBER", "PLUS". */
  type: string
  /** 1-based source line. */
  line: number
  /** 0-based column within the line. */
  col: number
  /** Source lexeme (exact for NAME/NUMBER/STRING, representative otherwise). */
  text: string
}

/** Operand of a bytecode instruction; shape depends on `format`. */
export type Operand =
  | null
  | string
  | number
  | { target: number; delta: number } // MOF_JUMP

export type OpFormat = 'byte' | 'jump' | 'incop' | 'argc' | 'const'

/** One disassembled bytecode instruction (mo_bcode.c / mocha.def). */
export interface Instruction {
  /** Byte offset within its script's code. */
  offset: number
  /** Mnemonic, e.g. "add", "call", "ifeq". */
  op: string
  /** Instruction length in bytes. */
  length: number
  /** Stack slots consumed (-1 = variadic). */
  nuses: number
  /** Stack slots produced. */
  ndefs: number
  /** Source line this instruction came from. */
  line: number
  format: OpFormat
  operand: Operand
}

/** A compiled script: the top-level program or a function body. */
export interface Script {
  id: number
  /** "<main>" for the program, else the function name. */
  name: string
  bytecode: Instruction[]
}

/** A value-stack entry, captured without invoking user code. */
export interface Datum {
  type:
    | 'undefined'
    | 'number'
    | 'boolean'
    | 'string'
    | 'object'
    | 'function'
    | 'atom'
    | 'symbol'
    | 'internal'
    | 'unknown'
  value: string
}

/** Active call frame at a trace step. */
export interface Frame {
  fn: string
  args: Datum[]
  vars: Datum[]
}

/** One interpreter step, captured before the instruction is dispatched. */
export interface TraceStep {
  step: number
  /** Index into `RunResult.scripts` of the executing script. */
  script: number
  /** Byte offset (program counter) within that script. */
  pc: number
  /** Source line. */
  line: number
  /** Mnemonic about to execute. */
  op: string
  /** Call depth (0 = top level). */
  depth: number
  /** Value stack (most recent STACK_WINDOW entries). */
  stack: Datum[]
  frame: Frame | null
}

export interface MochaError {
  message: string
  line: number
  col: number
  linebuf: string
}

/** Full result of compiling and running a program. */
export interface RunResult {
  ok: boolean
  output: string
  error: MochaError | null
  result: Datum | null
  tokens: Token[]
  scripts: Script[]
  trace: TraceStep[]
  /** True if execution hit the step cap (runaway loop guard). */
  truncated: boolean
  stats: { steps: number; scripts: number }
}
