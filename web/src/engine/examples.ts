/** Curated sample programs that exercise the 1995 engine's features. */
export interface Example {
  id: string
  title: string
  blurb: string
  code: string
}

export const EXAMPLES: Example[] = [
  {
    id: 'hello',
    title: 'Hello, Mocha',
    blurb: 'The classic — a function call and print.',
    code: `function hello(name) {
  print('hello', name)
}
hello('mocha')`,
  },
  {
    id: 'factorial',
    title: 'Recursion',
    blurb: 'Recursive factorial — watch the call stack grow.',
    code: `function fact(n) {
  if (n < 2) return 1
  return n * fact(n - 1)
}
print('5! =', fact(5))`,
  },
  {
    id: 'loop',
    title: 'Loops & bytecode',
    blurb: 'A for-loop compiles to goto/ifne — see the jumps.',
    code: `var sum = 0
for (var i = 1; i <= 10; i = i + 1) {
  sum = sum + i
}
print('sum 1..10 =', sum)`,
  },
  {
    id: 'fib',
    title: 'Fibonacci',
    blurb: 'Tree recursion — a deep, branchy execution trace.',
    code: `function fib(n) {
  if (n < 2) return n
  return fib(n - 1) + fib(n - 2)
}
print('fib(7) =', fib(7))`,
  },
  {
    id: 'operators',
    title: 'Operators',
    blurb: 'Arithmetic, comparison and bitwise opcodes.',
    code: `var a = 6, b = 4
print('add', a + b)
print('mul', a * b)
print('cmp', a > b)
print('bit', a & b, a | b, a ^ b)
print('shift', a << 1, a >> 1)`,
  },
  {
    id: 'conditional',
    title: 'Conditionals',
    blurb: 'if/else and the ?: operator branch the bytecode.',
    code: `function classify(n) {
  if (n < 0) return 'negative'
  return n == 0 ? 'zero' : 'positive'
}
print(classify(-3))
print(classify(0))
print(classify(42))`,
  },
]

export const DEFAULT_CODE = EXAMPLES[1].code
