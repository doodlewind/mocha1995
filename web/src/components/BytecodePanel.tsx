/**
 * Bytecode disassembly view: shows the compiled instructions for the selected
 * script, highlights the instruction at the current trace step's program
 * counter, and draws jump arrows in a left gutter connecting goto/ifeq/ifne
 * instructions to their target offsets.
 */
import { useEffect, useMemo, useRef, useState } from 'react'
import clsx from 'clsx'
import { useStore, useCurrentStep } from '../store'
import type { Instruction, Operand } from '../engine/types'
import { opStyle, CATEGORY_STYLE } from '../theme'
import { Panel, CountBadge, EmptyState } from './Panel'

/** Fixed row height (px) — kept in sync with the row styling for arrow geometry. */
const ROW_H = 22
/** Width (px) of the left gutter reserved for jump arrows. */
const GUTTER_W = 24

/** Format an instruction's operand into a short display string. */
function formatOperand(op: string, format: string, operand: Operand): string {
  switch (format) {
    case 'jump':
      if (operand && typeof operand === 'object') {
        return `-> @${operand.target}`
      }
      return ''
    case 'const':
      return typeof operand === 'string' ? operand : ''
    case 'argc':
      return typeof operand === 'number' ? `(${operand} arg${operand === 1 ? '' : 's'})` : ''
    case 'incop':
      return typeof operand === 'string' ? `(${operand})` : ''
    default:
      return ''
  }
}

interface JumpArrow {
  fromIndex: number
  toIndex: number
  back: boolean
}

export function BytecodePanel() {
  const scripts = useStore((s) => s.result?.scripts) ?? []
  const current = useCurrentStep()

  // Track the selected script by its array index (TraceStep.script is an index).
  const [selected, setSelected] = useState(0)

  // Sync selection to the script of the current trace step.
  useEffect(() => {
    if (current && current.script >= 0 && current.script < scripts.length) {
      setSelected(current.script)
    }
  }, [current, scripts.length])

  // Clamp selection if scripts change underneath us.
  const selectedIndex = selected < scripts.length ? selected : 0
  const script = scripts[selectedIndex]
  const bytecode = useMemo<Instruction[]>(() => script?.bytecode ?? [], [script])

  // Map byte offset -> row index for the selected script (for jump targets).
  const offsetToIndex = useMemo(() => {
    const map = new Map<number, number>()
    bytecode.forEach((ins, i) => map.set(ins.offset, i))
    return map
  }, [bytecode])

  // Compute jump arrows for the selected script.
  const arrows = useMemo<JumpArrow[]>(() => {
    const out: JumpArrow[] = []
    bytecode.forEach((ins, i) => {
      if (ins.format === 'jump' && ins.operand && typeof ins.operand === 'object') {
        const toIndex = offsetToIndex.get(ins.operand.target)
        if (toIndex !== undefined) {
          out.push({ fromIndex: i, toIndex, back: ins.operand.delta < 0 })
        }
      }
    })
    return out
  }, [bytecode, offsetToIndex])

  const isCurrentScript = current?.script === selectedIndex
  const activeRef = useRef<HTMLDivElement | null>(null)

  // Scroll the active instruction into view when it changes.
  useEffect(() => {
    if (isCurrentScript) {
      activeRef.current?.scrollIntoView({ block: 'nearest' })
    }
  }, [isCurrentScript, current?.pc])

  const arrowColor = opStyle('goto').hex

  const legend = (
    <div className="flex flex-wrap items-center gap-1">
      {(Object.keys(CATEGORY_STYLE) as Array<keyof typeof CATEGORY_STYLE>).map((cat) => (
        <span
          key={cat}
          className={clsx(
            'rounded border px-1.5 py-0.5 text-[10px] font-medium leading-none',
            CATEGORY_STYLE[cat].chip,
          )}
        >
          {CATEGORY_STYLE[cat].label}
        </span>
      ))}
    </div>
  )

  if (!script || bytecode.length === 0) {
    return (
      <Panel
        title="Bytecode"
        subtitle="mo_emit.c / mocha.def — compiled instructions"
      >
        <EmptyState>No bytecode yet. Run a program to disassemble it.</EmptyState>
      </Panel>
    )
  }

  const svgHeight = bytecode.length * ROW_H

  return (
    <Panel
      title="Bytecode"
      subtitle="mo_emit.c / mocha.def — compiled instructions"
      badge={<CountBadge>{bytecode.length}</CountBadge>}
      actions={legend}
      bodyClassName="flex flex-col"
    >
      {scripts.length > 1 && (
        <div className="flex shrink-0 flex-wrap gap-1 border-b border-ink-600 bg-ink-800/60 px-2 py-1.5">
          {scripts.map((s, i) => (
            <button
              key={s.id}
              type="button"
              onClick={() => setSelected(i)}
              className={clsx(
                'rounded px-2 py-0.5 font-mono text-[11px] transition-colors',
                i === selectedIndex
                  ? 'bg-accent/20 text-accent-bright ring-1 ring-accent/40'
                  : 'bg-ink-700 text-ink-300 hover:bg-ink-600 hover:text-ink-100',
              )}
            >
              {s.name}
            </button>
          ))}
        </div>
      )}

      <div className="relative min-h-0 flex-1 overflow-auto font-mono text-[12px]">
        {/* Jump-arrow gutter overlay. */}
        <svg
          className="pointer-events-none absolute left-0 top-0"
          width={GUTTER_W}
          height={svgHeight}
          style={{ overflow: 'visible' }}
          aria-hidden
        >
          {arrows.map((arrow, idx) => {
            const y1 = arrow.fromIndex * ROW_H + ROW_H / 2
            const y2 = arrow.toIndex * ROW_H + ROW_H / 2
            const xEdge = GUTTER_W - 3
            const xBend = 6
            // A simple rounded "bracket" connecting source to target.
            const d = `M ${xEdge} ${y1} L ${xBend} ${y1} L ${xBend} ${y2} L ${xEdge} ${y2}`
            return (
              <g key={idx} stroke={arrowColor} fill="none" opacity={0.55}>
                <path d={d} strokeWidth={1} />
                {/* Arrowhead pointing into the target row. */}
                <path
                  d={`M ${xEdge} ${y2} l -4 -3 m 4 3 l -4 3`}
                  strokeWidth={1}
                />
              </g>
            )
          })}
        </svg>

        <div style={{ paddingLeft: GUTTER_W }}>
          {bytecode.map((ins, i) => {
            const style = opStyle(ins.op)
            const operandText = formatOperand(ins.op, ins.format, ins.operand)
            const active = isCurrentScript && current?.pc === ins.offset
            const isJump = ins.format === 'jump' && ins.operand && typeof ins.operand === 'object'
            const back =
              isJump && typeof ins.operand === 'object' && ins.operand !== null
                ? ins.operand.delta < 0
                : false
            return (
              <div
                key={ins.offset}
                ref={active ? activeRef : undefined}
                className={clsx(
                  'flex items-center gap-2 px-3 pr-3',
                  active
                    ? 'border-l-2 border-accent bg-accent/15'
                    : 'border-l-2 border-transparent hover:bg-ink-700/40',
                )}
                style={{ height: ROW_H }}
              >
                <span className="w-12 shrink-0 text-right text-ink-500 tabular-nums">
                  {String(ins.offset).padStart(5, '0')}
                </span>
                <span
                  className="shrink-0 font-semibold"
                  style={{ color: style.hex }}
                >
                  {ins.op}
                </span>
                {operandText && (
                  <span className="truncate text-ink-300">{operandText}</span>
                )}
                {isJump && (
                  <span
                    className="ml-auto shrink-0 text-[10px] leading-none text-ink-500"
                    title={back ? 'back-edge' : 'forward jump'}
                  >
                    {back ? '▲' : '▼'}
                  </span>
                )}
              </div>
            )
          })}
        </div>
      </div>
    </Panel>
  )
}
