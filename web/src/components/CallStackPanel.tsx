/**
 * Call stack visualizer. Reconstructs the chain of active function frames at
 * the current trace step and renders them top-down (innermost first), so the
 * shape of the running call stack is legible at a glance.
 */
import { useMemo } from 'react'
import clsx from 'clsx'
import { useStore, useCurrentStep } from '../store'
import type { Datum, Frame, TraceStep } from '../engine/types'
import { datumColor } from '../theme'
import { Panel, CountBadge, EmptyState } from './Panel'

/** A frame as rendered: depth + the function name and its captured data. */
interface ChainFrame {
  depth: number
  fn: string
  args: Datum[]
  vars: Datum[]
  /** True for the innermost / currently-executing frame. */
  current: boolean
}

/**
 * Walk backwards from the current step to find, for each depth level
 * d = current.depth .. 1, the most recent step at exactly that depth and use
 * its frame. Depth 0 is always the synthetic "<main>" frame at the bottom.
 */
function buildChain(
  trace: TraceStep[],
  stepIndex: number,
  current: TraceStep,
): ChainFrame[] {
  const chain: ChainFrame[] = []

  for (let level = current.depth; level >= 1; level--) {
    let found: Frame | null = null
    for (let i = stepIndex; i >= 0; i--) {
      const s = trace[i]
      if (s.depth === level && s.frame) {
        found = s.frame
        break
      }
    }
    chain.push({
      depth: level,
      fn: found?.fn ?? '(anonymous)',
      args: found?.args ?? [],
      vars: found?.vars ?? [],
      current: level === current.depth,
    })
  }

  // Bottom of the stack: the top-level program.
  chain.push({
    depth: 0,
    fn: '<main>',
    args: [],
    vars: [],
    current: current.depth === 0,
  })

  return chain
}

function ValueChip({ datum }: { datum: Datum }) {
  const color = datumColor(datum.type)
  return (
    <span
      className="inline-flex max-w-full items-center gap-1 rounded border border-ink-600 bg-ink-900/50 px-1.5 py-0.5 font-mono text-[11px]"
      title={`${datum.type}: ${datum.value}`}
    >
      <span
        className="h-1.5 w-1.5 shrink-0 rounded-full"
        style={{ backgroundColor: color }}
        aria-hidden
      />
      <span className="truncate" style={{ color }}>
        {datum.value}
      </span>
    </span>
  )
}

function FrameCard({ frame }: { frame: ChainFrame }) {
  const { current } = frame
  return (
    <li
      className={clsx(
        'rounded-lg border px-3 py-2 transition-colors',
        current
          ? 'border-accent/60 bg-accent/10'
          : 'border-ink-600 bg-ink-800/60',
      )}
    >
      <div className="flex items-center justify-between gap-2">
        <span
          className={clsx(
            'truncate font-mono text-sm font-semibold',
            current ? 'text-accent-bright' : 'text-ink-100',
          )}
        >
          {frame.fn}
        </span>
        <span
          className={clsx(
            'shrink-0 rounded-full border px-1.5 py-0.5 text-[10px] font-medium uppercase tracking-wide',
            current
              ? 'border-accent/40 text-accent'
              : 'border-ink-600 text-ink-300',
          )}
        >
          depth {frame.depth}
        </span>
      </div>

      {frame.args.length > 0 && (
        <div className="mt-2">
          <div className="mb-1 text-[10px] uppercase tracking-wide text-ink-400">
            args
          </div>
          <div className="flex flex-wrap gap-1">
            {frame.args.map((d, i) => (
              <ValueChip key={i} datum={d} />
            ))}
          </div>
        </div>
      )}

      {frame.vars.length > 0 && (
        <div className="mt-2">
          <div className="mb-1 flex items-center gap-1 text-[10px] uppercase tracking-wide text-ink-400">
            <span>vars</span>
            <CountBadge>{frame.vars.length}</CountBadge>
          </div>
          <div className="flex flex-wrap gap-1">
            {frame.vars.map((d, i) => (
              <ValueChip key={i} datum={d} />
            ))}
          </div>
        </div>
      )}

      {frame.depth > 0 &&
        frame.args.length === 0 &&
        frame.vars.length === 0 && (
          <div className="mt-1 text-[11px] italic text-ink-400">
            no locals
          </div>
        )}
    </li>
  )
}

export function CallStackPanel() {
  const result = useStore((s) => s.result)
  const stepIndex = useStore((s) => s.stepIndex)
  const step = useCurrentStep()

  const chain = useMemo<ChainFrame[]>(() => {
    if (!result || !step || stepIndex < 0) return []
    return buildChain(result.trace, stepIndex, step)
  }, [result, step, stepIndex])

  return (
    <Panel
      title="Call stack"
      subtitle="active function frames"
      badge={chain.length > 0 ? <CountBadge>{chain.length}</CountBadge> : null}
      bodyClassName="p-3"
    >
      {chain.length === 0 ? (
        <EmptyState>
          {result
            ? 'Step through the trace to inspect call frames.'
            : 'Run a program to inspect the call stack.'}
        </EmptyState>
      ) : (
        <ol className="flex flex-col gap-2">
          {chain.map((frame) => (
            <FrameCard key={frame.depth} frame={frame} />
          ))}
        </ol>
      )}
    </Panel>
  )
}
