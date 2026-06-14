/**
 * Shared visual language: maps token classes and bytecode opcodes to colour
 * categories so the token view, bytecode view, trace and charts agree.
 * Colours are plain hex (used in SVG and inline styles); Tailwind classes are
 * provided where a utility string is more convenient.
 */

export interface CategoryStyle {
  /** Hex colour for SVG / inline styles. */
  hex: string
  /** Tailwind text + bg/border classes for chips. */
  chip: string
  label: string
}

/** Broad opcode categories used for colour-coding instructions and the stack. */
export type OpCategory =
  | 'load' // push constants/names
  | 'arith' // +,-,*,/ ...
  | 'logic' // comparisons, bitwise, not
  | 'control' // goto/ifeq/ifne/return
  | 'call' // call/new
  | 'store' // assign/inc/dec
  | 'member' // member/index access
  | 'stack' // push/pop/dup
  | 'other'

export const OP_CATEGORY: Record<string, OpCategory> = {
  push: 'stack', pop: 'stack', dup: 'stack', nop: 'other',
  name: 'load', number: 'load', string: 'load', zero: 'load', one: 'load',
  null: 'load', this: 'load', true: 'load', false: 'load',
  add: 'arith', sub: 'arith', mul: 'arith', div: 'arith', mod: 'arith',
  neg: 'arith',
  bitor: 'logic', bitxor: 'logic', bitand: 'logic', bitnot: 'logic',
  eq: 'logic', ne: 'logic', lt: 'logic', le: 'logic', gt: 'logic', ge: 'logic',
  lsh: 'logic', rsh: 'logic', ursh: 'logic', not: 'logic', in: 'logic',
  typeof: 'logic', void: 'logic',
  goto: 'control', ifeq: 'control', ifne: 'control', return: 'control',
  enter: 'control', leave: 'control', trap: 'control',
  call: 'call', new: 'call',
  assign: 'store', inc: 'store', dec: 'store', delete: 'store',
  member: 'member', lmember: 'member', index: 'member', lindex: 'member',
}

export const CATEGORY_STYLE: Record<OpCategory, CategoryStyle> = {
  load: { hex: '#5ec5d6', chip: 'text-cyan-200 bg-cyan-500/15 border-cyan-500/30', label: 'Load' },
  arith: { hex: '#f4b063', chip: 'text-amber-200 bg-amber-500/15 border-amber-500/30', label: 'Arithmetic' },
  logic: { hex: '#c490e4', chip: 'text-purple-200 bg-purple-500/15 border-purple-500/30', label: 'Logic' },
  control: { hex: '#ef6f6f', chip: 'text-red-200 bg-red-500/15 border-red-500/30', label: 'Control flow' },
  call: { hex: '#7ee08a', chip: 'text-green-200 bg-green-500/15 border-green-500/30', label: 'Call' },
  store: { hex: '#e0954a', chip: 'text-orange-200 bg-orange-500/15 border-orange-500/30', label: 'Store' },
  member: { hex: '#8ab4f8', chip: 'text-blue-200 bg-blue-500/15 border-blue-500/30', label: 'Member' },
  stack: { hex: '#9aa7b3', chip: 'text-slate-200 bg-slate-500/15 border-slate-500/30', label: 'Stack' },
  other: { hex: '#6b7682', chip: 'text-slate-300 bg-slate-600/15 border-slate-600/30', label: 'Other' },
}

export function opCategory(op: string): OpCategory {
  return OP_CATEGORY[op] ?? 'other'
}

export function opStyle(op: string): CategoryStyle {
  return CATEGORY_STYLE[opCategory(op)]
}

/** Token classes grouped for colouring the scanner output. */
export type TokenGroup =
  | 'keyword'
  | 'identifier'
  | 'number'
  | 'string'
  | 'operator'
  | 'punct'
  | 'primary'

const KEYWORDS = new Set([
  'FUNCTION', 'IF', 'ELSE', 'SWITCH', 'CASE', 'DEFAULT', 'WHILE', 'DO', 'FOR',
  'BREAK', 'CONTINUE', 'IN', 'VAR', 'WITH', 'RETURN', 'NEW', 'RESERVED',
])
const OPERATORS = new Set([
  'ASSIGN', 'HOOK', 'COLON', 'OR', 'AND', 'BITOR', 'BITXOR', 'BITAND', 'EQOP',
  'RELOP', 'SHOP', 'PLUS', 'MINUS', 'MULOP', 'UNARYOP', 'INCOP', 'DOT',
])
const PUNCT = new Set([
  'SEMI', 'LB', 'RB', 'LC', 'RC', 'LP', 'RP', 'COMMA', 'EOL', 'EOF',
])

export function tokenGroup(type: string): TokenGroup {
  if (KEYWORDS.has(type)) return 'keyword'
  if (OPERATORS.has(type)) return 'operator'
  if (PUNCT.has(type)) return 'punct'
  if (type === 'NAME') return 'identifier'
  if (type === 'NUMBER') return 'number'
  if (type === 'STRING') return 'string'
  if (type === 'PRIMARY') return 'primary'
  return 'punct'
}

export const TOKEN_STYLE: Record<TokenGroup, CategoryStyle> = {
  keyword: { hex: '#c490e4', chip: 'text-purple-200 bg-purple-500/15 border-purple-500/30', label: 'Keyword' },
  identifier: { hex: '#8ab4f8', chip: 'text-blue-200 bg-blue-500/15 border-blue-500/30', label: 'Identifier' },
  number: { hex: '#f4b063', chip: 'text-amber-200 bg-amber-500/15 border-amber-500/30', label: 'Number' },
  string: { hex: '#7ee08a', chip: 'text-green-200 bg-green-500/15 border-green-500/30', label: 'String' },
  operator: { hex: '#5ec5d6', chip: 'text-cyan-200 bg-cyan-500/15 border-cyan-500/30', label: 'Operator' },
  punct: { hex: '#9aa7b3', chip: 'text-slate-300 bg-slate-500/15 border-slate-500/30', label: 'Punctuation' },
  primary: { hex: '#ef9f6f', chip: 'text-orange-200 bg-orange-500/15 border-orange-500/30', label: 'Primary' },
}

export function tokenStyle(type: string): CategoryStyle {
  return TOKEN_STYLE[tokenGroup(type)]
}

/** Colour for a datum on the value stack, by runtime type. */
export function datumColor(type: string): string {
  switch (type) {
    case 'number': return '#f4b063'
    case 'string': return '#7ee08a'
    case 'boolean': return '#c490e4'
    case 'object': return '#8ab4f8'
    case 'function': return '#5ec5d6'
    case 'undefined': return '#6b7682'
    default: return '#9aa7b3'
  }
}
