/** Opcode frequency bar chart: tallies executed bytecodes across the trace. */
import { useMemo } from 'react'
import clsx from 'clsx'
import { useStore, useCurrentStep } from '../../store'
import {
  opStyle,
  opCategory,
  CATEGORY_STYLE,
  type OpCategory,
} from '../../theme'
import { Panel, CountBadge, EmptyState } from '../Panel'

interface OpCount {
  op: string
  count: number
}

const CATEGORY_ORDER: OpCategory[] = [
  'load',
  'arith',
  'logic',
  'control',
  'call',
  'store',
  'member',
  'stack',
  'other',
]

export function OpcodeChart() {
  const result = useStore((s) => s.result)
  const current = useCurrentStep()
  const trace = result?.trace ?? []

  const { rows, max, total, categoryTotals } = useMemo(() => {
    const counts = new Map<string, number>()
    const cats = new Map<OpCategory, number>()
    for (const step of trace) {
      counts.set(step.op, (counts.get(step.op) ?? 0) + 1)
      const cat = opCategory(step.op)
      cats.set(cat, (cats.get(cat) ?? 0) + 1)
    }
    const rows: OpCount[] = Array.from(counts, ([op, count]) => ({ op, count }))
    rows.sort((a, b) => b.count - a.count || a.op.localeCompare(b.op))
    const max = rows.length > 0 ? rows[0].count : 0
    const total = trace.length
    return { rows, max, total, categoryTotals: cats }
  }, [trace])

  const currentOp = current?.op ?? null

  return (
    <Panel
      title="Opcode profile"
      subtitle="bytecodes executed, by frequency"
      badge={rows.length > 0 ? <CountBadge>{rows.length}</CountBadge> : undefined}
    >
      {rows.length === 0 ? (
        <EmptyState>No bytecodes executed yet. Run a script to profile it.</EmptyState>
      ) : (
        <div className="flex h-full min-h-0 flex-col">
          {/* Category legend with per-category totals. */}
          <div className="flex flex-wrap gap-1.5 border-b border-ink-700 px-3 py-2">
            {CATEGORY_ORDER.map((cat) => {
              const n = categoryTotals.get(cat) ?? 0
              if (n === 0) return null
              const style = CATEGORY_STYLE[cat]
              return (
                <span
                  key={cat}
                  className={clsx(
                    'inline-flex items-center gap-1.5 rounded-full border px-2 py-0.5 text-[10px] font-medium',
                    style.chip,
                  )}
                >
                  <span
                    className="h-2 w-2 shrink-0 rounded-full"
                    style={{ backgroundColor: style.hex }}
                  />
                  {style.label}
                  <span className="font-mono tabular-nums opacity-80">{n}</span>
                </span>
              )
            })}
          </div>

          {/* Bars. */}
          <div className="min-h-0 flex-1 overflow-auto px-3 py-2">
            <ul className="flex flex-col gap-1">
              {rows.map(({ op, count }) => {
                const style = opStyle(op)
                const pct = max > 0 ? (count / max) * 100 : 0
                const share = total > 0 ? (count / total) * 100 : 0
                const isCurrent = op === currentOp
                return (
                  <li
                    key={op}
                    className={clsx(
                      'flex items-center gap-2 rounded-md px-1 py-0.5 transition-colors',
                      isCurrent && 'bg-ink-700/60 ring-1 ring-accent',
                    )}
                  >
                    <span
                      className="w-20 shrink-0 truncate text-right font-mono text-xs"
                      style={{ color: style.hex }}
                      title={op}
                    >
                      {op}
                    </span>
                    <div className="relative h-4 min-w-0 flex-1 overflow-hidden rounded-sm bg-ink-700/40">
                      <div
                        className="h-full rounded-sm transition-[width] duration-300"
                        style={{
                          width: `${pct}%`,
                          backgroundColor: style.hex,
                          opacity: isCurrent ? 1 : 0.78,
                        }}
                      />
                    </div>
                    <span
                      className="w-14 shrink-0 text-right font-mono text-[11px] tabular-nums text-ink-200"
                      title={`${share.toFixed(1)}% of executed bytecodes`}
                    >
                      {count}
                    </span>
                  </li>
                )
              })}
            </ul>
          </div>
        </div>
      )}
    </Panel>
  )
}
