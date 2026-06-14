/** Value-stack visualizer: shows the operand stack at the current trace step. */
import { useCurrentStep } from '../store'
import { datumColor } from '../theme'
import type { Datum } from '../engine/types'
import { Panel, CountBadge, EmptyState } from './Panel'

interface CellProps {
  datum: Datum
  /** Stack index bottom→top (0 = bottom). Used as the animation key. */
  index: number
  isTop: boolean
}

function StackCell({ datum, index, isTop }: CellProps) {
  const color = datumColor(datum.type)
  return (
    <div
      key={index}
      className="animate-pop-in flex items-stretch gap-2 overflow-hidden rounded-lg border border-ink-600 bg-ink-700/50"
    >
      <span
        className="w-1.5 shrink-0 rounded-l-lg"
        style={{ backgroundColor: color }}
        aria-hidden
      />
      <div className="flex min-w-0 flex-1 items-center justify-between gap-3 py-1.5 pr-2.5">
        <span
          className="truncate font-mono text-sm text-ink-100"
          style={{ color }}
          title={datum.value}
        >
          {datum.value}
        </span>
        <span className="flex shrink-0 items-center gap-1.5">
          {isTop && (
            <span className="text-[10px] font-medium tracking-wide text-accent">
              ← top
            </span>
          )}
          <span className="rounded bg-ink-600 px-1.5 py-0.5 font-mono text-[10px] text-ink-300">
            {datum.type}
          </span>
        </span>
      </div>
    </div>
  )
}

export function StackPanel() {
  const step = useCurrentStep()
  const stack: Datum[] = step?.stack ?? []

  if (!step) {
    return (
      <Panel title="Value stack" subtitle="operand stack at current step">
        <EmptyState>Run a program and step through it to inspect the operand stack.</EmptyState>
      </Panel>
    )
  }

  // Render top-of-stack first: stack is bottom→top, so reverse.
  const cells = stack
    .map((datum, index) => ({ datum, index }))
    .reverse()

  return (
    <Panel
      title="Value stack"
      subtitle="operand stack at current step"
      badge={<CountBadge>{stack.length}</CountBadge>}
      bodyClassName="flex flex-col"
    >
      <div className="border-b border-ink-600 px-3 py-2 text-[11px] text-ink-300">
        depth {step.depth} · about to run{' '}
        <span className="font-mono text-ink-100">'{step.op}'</span>
      </div>
      <div className="min-h-0 flex-1 overflow-auto p-3">
        {stack.length === 0 ? (
          <div className="py-6 text-center font-mono text-xs text-ink-400">
            (empty stack)
          </div>
        ) : (
          <div className="flex flex-col gap-1.5">
            {cells.map(({ datum, index }, renderPos) => (
              <StackCell
                key={index}
                datum={datum}
                index={index}
                isTop={renderPos === 0}
              />
            ))}
          </div>
        )}
      </div>
    </Panel>
  )
}
